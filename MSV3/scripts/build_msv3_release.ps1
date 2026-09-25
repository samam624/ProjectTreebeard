param(
  [string]$ReleaseVersion = "1.0.1",
  # 65538 is the first version built from source where DEVICE_NAME is derived from
  # SENSOR_ID. scripts/msv3_fleet.py treats anything below it as a package whose advertised
  # name cannot be trusted to equal its sensor ID, and ble_ota_update.py refuses those
  # without --allow-legacy-package. Do not lower this default.
  [uint32]$ApplicationVersion = 65538,
  [string]$SigningKey,
  [string]$Python,
  [switch]$GenerateSigningKey,
  [switch]$SkipApplicationCompile,
  # Bench builds only. Forwarded to arduino_msv3_probe.ps1, which turns it into
  # -DMSV3_ENABLE_SERIAL_LOGS=1. Fleet images ship with logging off.
  [switch]$SerialLogs,
  # Guard 2 and the deliberate-hang acceptance image. Forwarded to arduino_msv3_probe.ps1,
  # which refuses -HangTest for any sensor id except the bench board.
  [switch]$BootGuard,
  [switch]$HangTest,
  # Which probe this release's APPLICATION package is for. Required unless the application build
  # is skipped: since 2026-09-05 the sketch takes its identity as a build parameter, so a release
  # is per-probe and the package name says which. The bootloader half is identity-free and shared.
  [int]$SensorId = -1,
  [string]$BuildTag,
  [ValidateRange(1, 65535)]
  [int]$FdcSettleCount = 100,
  [ValidateRange(0, 255)]
  [int]$WakeDiscardConversions = 2,
  [ValidateRange(0, 31)]
  [int]$FdcIdriveCode = 16,
  [ValidateSet(10, 33)]
  [int]$FdcDeglitchMhz = 10,
  # Also emit a SIGNED SD+BL+APP package tagged 0x3356, for delivering this bootloader release to a
  # probe already running an MSV3 bootloader. Off by default: an ordinary release is
  # application-only, and a bootloader must be asked for.
  [switch]$BootloaderOta,
  # Forwarded to arduino_msv3_probe.ps1. Probe-specific experiment image; see that script.
  [switch]$HybridV12Tank,
  [ValidateSet(0, 2)]
  [int]$HybridMuxCode = 2
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$msv3Root = (Resolve-Path (Join-Path $scriptDir "..")).Path
$repoRoot = (Resolve-Path (Join-Path $msv3Root "..")).Path
$cacheRoot = Join-Path $msv3Root ".arduino-build"
$source = Join-Path $cacheRoot "Adafruit_nRF52_Bootloader"
$boardSource = Join-Path $msv3Root "bootloader\msv3_e73"
$boardDest = Join-Path $source "src\boards\msv3_e73"
. (Join-Path $scriptDir "msv3_python.ps1")
$pythonExe = Resolve-Msv3Python -Explicit $Python -Msv3Root $msv3Root
$keyPath = if ([string]::IsNullOrWhiteSpace($SigningKey)) {
  Join-Path $msv3Root ".secrets\msv3-dfu-signing-private.pem"
} elseif ([IO.Path]::IsPathRooted($SigningKey)) {
  $SigningKey
} else {
  Join-Path $repoRoot $SigningKey
}

if ($ReleaseVersion -notmatch '^\d+\.\d+\.\d+$') {
  throw "ReleaseVersion must be semantic numeric form, for example 1.0.0"
}
if ($ApplicationVersion -eq 0 -or $ApplicationVersion -eq [uint32]::MaxValue) {
  throw "ApplicationVersion must be explicit and cannot be 0 or 0xFFFFFFFF"
}

$nrfutil = (& (Join-Path $scriptDir "prepare_msv3_dfu_tools.ps1") -Python $pythonExe | Select-Object -Last 1)
$deps = Join-Path $cacheRoot "dfu-pydeps"
$env:PYTHONPATH = $deps

if (!(Test-Path -LiteralPath $keyPath)) {
  if (!$GenerateSigningKey) {
    throw "Signing key not found at $keyPath. Restore the existing key, or explicitly pass -GenerateSigningKey when establishing a new SWD-installed trust root."
  }
  & $pythonExe (Join-Path $scriptDir "generate_msv3_dfu_key.py") $keyPath
  if ($LASTEXITCODE -ne 0) { throw "MSV3 signing-key generation failed" }
}

$vkCode = (& $nrfutil keys --show-vk code $keyPath | Out-String)
if ($LASTEXITCODE -ne 0) { throw "Could not derive verification key" }
$qxMatch = [regex]::Match($vkCode, 'Qx\[\]\s*=\s*\{\s*(.*?)\s*\}', 'Singleline')
$qyMatch = [regex]::Match($vkCode, 'Qy\[\]\s*=\s*\{\s*(.*?)\s*\}', 'Singleline')
if (!$qxMatch.Success -or !$qyMatch.Success) { throw "Unexpected verification-key output" }
$qx = $qxMatch.Groups[1].Value -replace '\s+', ''
$qy = $qyMatch.Groups[1].Value -replace '\s+', ''

if (!(Test-Path -LiteralPath $source)) {
  New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
  & git clone --branch 0.11.0 --recurse-submodules `
    https://github.com/adafruit/Adafruit_nRF52_Bootloader.git $source
  if ($LASTEXITCODE -ne 0) { throw "Could not clone pinned Adafruit bootloader source" }
}

$requiredSource = Join-Path $source "lib\softdevice\s140_nrf52_6.1.1\s140_nrf52_6.1.1_softdevice.hex"
if (!(Test-Path -LiteralPath $requiredSource)) {
  throw "Pinned bootloader checkout is incomplete; missing $requiredSource"
}

# Return the pinned checkout to its stock state before applying anything. Every source change
# below is reapplied from tracked files on each run, so a rebuild is deterministic no matter what
# a previous run (or a hand edit) left behind. The whole clone lives under the gitignored
# .arduino-build/, so nothing in it is part of this repository and nothing in it may be the only
# copy of a change -- see the repository CLAUDE.md rule about edits outside the tree.
if (Test-Path -LiteralPath $boardDest) { Remove-Item -LiteralPath $boardDest -Recurse -Force }

& git -C $source checkout -- lib src linker
if ($LASTEXITCODE -ne 0) { throw "Could not reset the pinned bootloader checkout to its stock state" }

# Submodules need their own reset. `git checkout -- lib` restores the superproject's gitlinks but
# does NOT clean a submodule's working tree, so a modified file inside lib/tinyusb, lib/nrfx,
# lib/uf2 or lib/tinycrypt survives it. Measured 2026-09-06 by deliberately dirtying
# lib/tinyusb/src/tusb.c: the superproject files were restored and the submodule stayed modified.
# TinyUSB is compiled into the bootloader, so that would silently change the one binary that can
# never be re-flashed, and the sentinel grep below would not notice.
& git -C $source submodule foreach --quiet --recursive 'git checkout -- .'
if ($LASTEXITCODE -ne 0) { throw "Could not reset the pinned bootloader checkout's submodules" }

# Then refuse to build from anything still dirty, rather than trusting the two resets above to
# have been exhaustive. Fail closed: an unexplained modification here ends up inside an immutable
# bootloader, and no downstream check would attribute it.
$residual = @(& git -C $source status --porcelain -- lib src linker |
              Where-Object { $_ -and ($_ -notmatch 'src/boards/msv3_e73') })
if ($residual.Count -gt 0) {
  throw @"
The pinned bootloader checkout is still modified after resetting it:
  $($residual -join "`n  ")
Refusing to build. Every MSV3 source change is reapplied from tracked files on each run, so
anything left here is unexplained and would end up inside a bootloader that cannot be re-flashed
once a probe is coated. Investigate, or delete $source and let this script re-clone it.
"@
}

# MSV3 source patches, applied in release order. 1.0.0 and 1.0.1 take none: both are installed on
# real hardware right now and must stay byte-reproducible from this script.
#
# 1.0.2 adds on-device anti-rollback. Measured on probe 99 2026-09-05, stock 0.11.0 installs an
# OLDER validly signed image without complaint, because dfu_init.c ships an explicit empty template
# where the version check belongs. That matters for a coated probe: rolling back past guard 1
# restores the defect where a sensor fault closes the radio.
$patches = @()
if ([version]$ReleaseVersion -ge [version]"1.0.2") {
  $patches += @{
    File = Join-Path $msv3Root "bootloader\patches\msv3-1.0.2-anti-rollback.patch"
    # Grepped for after the patch applies. A patch that silently fails to apply would produce a
    # bootloader that looks right, installs, and quietly accepts rollbacks forever on hardware
    # whose SWD pads are under coating. Absence must fail the build, not the field.
    Sentinel = "MSV3_VERSION_FLOOR_MAGIC"
    SentinelIn = "lib\sdk11\components\libraries\bootloader_dfu\bootloader_types.h"
  }
}
if ([version]$ReleaseVersion -ge [version]"1.0.3") {
  $patches += @{
    File = Join-Path $msv3Root "bootloader\patches\msv3-1.0.3-boot-guard-retained-ram.patch"
    # Guard 2's counter lives at the final eight bytes of RAM. The application linker excludes
    # them too; if either side omits its reservation, the bootloader/application stack can erase
    # the very state meant to survive the reset between them.
    Sentinel = "MSV3_BOOT_GUARD_RETAINED"
    SentinelIn = "linker\nrf52840.ld"
  }
}
if ([version]$ReleaseVersion -ge [version]"1.0.4") {
  $patches += @{
    File = Join-Path $msv3Root "bootloader\patches\msv3-1.0.4-usb-ble-coexist.patch"
    # Brings up USB alongside BLE when VBUS is present, instead of stock 0.11.0's either/or. A
    # silent failure to apply would produce a bootloader that looks and installs correctly and
    # still has no wired escape -- which is the entire defect, on exactly the coated probes that
    # cannot be reached any other way.
    Sentinel = "MSV3_USB_BLE_COEXIST"
    SentinelIn = "src\main.c"
  }
}
if ([version]$ReleaseVersion -ge [version]"1.0.5") {
  $patches += @{
    File = Join-Path $msv3Root "bootloader\patches\msv3-1.0.5-serial-activate-quiesce.patch"
    # 1.0.4 made BLE advertise THROUGHOUT a wired transfer, and stock's serial STOP_DATA handler
    # calls dfu_image_activate() -- whose settings write is asynchronous pstorage -- with the radio
    # still live, throwing the return value away. The BLE transport quiesces itself first
    # (BLE_DFU_ACTIVATE_N_RESET closes the transport before activating); serial had no equivalent.
    # Measured 2026-09-20: six wired transfers delivered every byte, reported success, and left no
    # valid application. A silent failure to apply would restore exactly that, and the symptom is a
    # wired update that claims success -- the worst shape for a probe with no SWD.
    Sentinel = "MSV3_SERIAL_ACTIVATE_QUIESCE"
    SentinelIn = "lib\sdk11\components\libraries\bootloader_dfu\dfu_transport_serial.c"
  }
}
if ([version]$ReleaseVersion -ge [version]"1.0.6") {
  $patches += @{
    File = Join-Path $msv3Root "bootloader\patches\msv3-1.0.6-dfu-trace.patch"
    # A DIAGNOSTIC release. Stock throws away both dfu_image_validate() and dfu_image_activate()
    # return values on the serial path and the host client sends no validate/activate at all, so a
    # failed wired update is unattributable from either end. This retains a decision record across
    # the reset and publishes it as scan-response manufacturer data, which is readable by a passive
    # scan without touching the USB endpoint under investigation. It changes no security check.
    Sentinel = "MSV3_DFU_TRACE_MAGIC"
    SentinelIn = "lib\sdk11\components\libraries\bootloader_dfu\bootloader_types.h"
  }
}
if ([version]$ReleaseVersion -ge [version]"1.0.7") {
  $patches += @{
    File = Join-Path $msv3Root "bootloader\patches\msv3-1.0.7-serial-queue-credit.patch"
    # THE WIRED-UPDATE FIX. Stock sizes the serial packet queue at 4 (3 usable) while the HCI
    # receive pool is 16, so HCI acknowledges frames the application cannot retain and the
    # queue-full path discards them AFTER the host has been told they landed. That path does not
    # discriminate by packet type, so the single STOP_DATA packet is droppable too -- which is how
    # a wired update completed, reported success, never ran validate or activate, and left a board
    # with no application. If this patch silently failed to apply, that behaviour would return and
    # the symptom is a wired flash that CLAIMS SUCCESS, which is the worst possible failure shape
    # for a probe with no SWD.
    Sentinel = "MSV3_SERIAL_QUEUE_CREDIT"
    SentinelIn = "lib\sdk11\components\libraries\bootloader_dfu\dfu_transport_serial.c"
  }
}
if ([version]$ReleaseVersion -ge [version]"1.0.8") {
  $patches += @{
    File = Join-Path $msv3Root "bootloader\patches\msv3-1.0.8-serial-rx-gate.patch"
    # The real flow control, and the fix 1.0.7 only approximated. HCI acknowledges a frame at
    # receive time, so no queue size can stop an acknowledged frame being discarded -- the fix has
    # to be upstream of the acknowledgement. This stops draining the USB CDC FIFO while the DFU
    # packet queue is near capacity, which NAKs the host at the USB layer instead. Requires nothing
    # of the host, which is the point: the stock Arduino IDE client gets it for free.
    Sentinel = "MSV3_SERIAL_RX_GATE"
    SentinelIn = "lib\sdk11\components\libraries\bootloader_dfu\dfu_transport_serial.c"
  }
}
if ([version]$ReleaseVersion -eq [version]"1.0.9") {
  # 1.0.9 IS WITHDRAWN. Measured on probe 8, 2026-09-20: it makes the wired path strictly worse --
  # the transfer now dies on the FIRST write instead of around frame 13, because is_ota() does not
  # select a transport, it selects whether flash goes through the SoftDevice. With the SoftDevice
  # enabled the asynchronous pstorage path is MANDATORY and a direct flash_nrf5x_* call faults
  # immediately. The real defect it was aiming at is real -- pstorage_store() is asynchronous and
  # reads from the HCI receive buffer that data_queue_element_free() recycles as soon as the
  # handler returns -- but the fix belongs in BUFFER LIFETIME (free the queue element from the
  # pstorage completion callback, or bounce-copy before the store), not in forcing the sync path.
  # The patch is kept only as the record of a disproven approach.
  throw @"
MSV3 bootloader 1.0.9 is WITHDRAWN and must not be built or installed.
It forces the synchronous flash path for wired sessions, which faults immediately when the
SoftDevice is enabled -- wired updates fail EARLIER on it than on 1.0.8.
See MSV3/docs/MSV3_USB_BLE_COEXISTENCE_FIRST_HARDWARE_2026-09-20.md. Build 1.0.8 instead, and fix
the serial transport's buffer lifetime rather than its flash path.
"@
}
if ([version]$ReleaseVersion -ge [version]"1.0.10") {
  $patches += @{
    File = Join-Path $msv3Root "bootloader\patches\msv3-1.0.10-serial-deferred-free.patch"
    # THE WIRED-UPDATE FIX. Since 1.0.4 runs both transports at once, a wired transfer takes the
    # ASYNCHRONOUS pstorage path (is_ota() reports the global _ota_dfu, not the transport) -- and
    # that path is handed a pointer into the HCI receive buffer which process_dfu_packet() then
    # recycled the instant the handler returned, while the SoftDevice was still reading it. The
    # buffer is now released from the write-completion callback instead. If this silently failed to
    # apply, wired updates would resume resetting the board mid-transfer with the COM port
    # vanishing, which is unattributable from the host.
    Sentinel = "MSV3_SERIAL_DEFERRED_FREE"
    SentinelIn = "lib\sdk11\components\libraries\bootloader_dfu\dfu_transport_serial.c"
  }
}
if ([version]$ReleaseVersion -ge [version]"1.0.11") {
  $patches += @{
    File = Join-Path $msv3Root "bootloader\patches\msv3-1.0.11-trace-nonce-opcodes.patch"
    # Makes the 1.0.6 trace trustworthy. Four different bootloaders were once read back
    # byte-identical, and with no nonce or build id there was no way to tell a failed fix from a
    # record that had never been re-armed -- so several releases were built on evidence that may
    # never have been evidence. Adds nonce + build id, rx_seen/queued/dropped separately,
    # stop_seen_at_rx vs stop_processed, RESETREAS, and the EXACT first
    # data_queue_element_alloc() return with the packet-type byte that was rejected. That last
    # value separates queue pressure (NO_MEM) from a type rejection (INVALID_PARAM), which need
    # opposite fixes.
    Sentinel = "MSV3_DFU_TRACE_NONCE"
    SentinelIn = "lib\sdk11\components\libraries\bootloader_dfu\bootloader_types.h"
  }
}
if ([version]$ReleaseVersion -ge [version]"1.0.12") {
  $patches += @{
    File = Join-Path $msv3Root "bootloader\patches\msv3-1.0.12-app-page-fingerprint.patch"
    # Advertises a CRC16 of the FIRST APPLICATION PAGE unconditionally, outside the trace-magic
    # gate. An absent trace record is ambiguous in the worst way -- it can mean START_PACKET was
    # never processed (application intact, board merely parked in DFU by the 1200-baud touch) or
    # that the record was armed and then destroyed, and those need opposite fixes. Comparing the
    # page fingerprint against the known-good application and against an erased page settles it
    # without reading any settings state.
    Sentinel = "MSV3_APP_PAGE_FINGERPRINT"
    SentinelIn = "lib\sdk11\components\libraries\bootloader_dfu\dfu_transport_ble.c"
  }
}
if ([version]$ReleaseVersion -ge [version]"1.0.13") {
  $patches += @{
    File = Join-Path $msv3Root "bootloader\patches\msv3-1.0.13-serial-tx-pump.patch"
    # THE WIRED DFU FIX. transmit_buffer() abandons a frame the moment the CDC TX FIFO refuses a
    # byte, and the only code that ever resumed it is in that file's non-USB branch. Under
    # NRF_USBD the SLIP TX state machine therefore sticks at SLIP_TRANSMITTING permanently, every
    # later acknowledgement is refused with NRF_ERROR_NO_MEM, and ack_transmit() discards that
    # return by design. The host stops being acknowledged, resets its HCI sequence number, and the
    # transfer dies with the application already erased. hci_slip_tx_pump() is the missing resume.
    # A silent failure to apply gives back a bootloader that enumerates, accepts START, erases the
    # application and then cannot finish -- the exact shape that cost eight diagnostic releases.
    Sentinel = "MSV3_SERIAL_TX_PUMP"
    SentinelIn = "lib\sdk\components\libraries\hci\hci_slip.c"
  }
}
if ([version]$ReleaseVersion -ge [version]"1.0.14") {
  $patches += @{
    File = Join-Path $msv3Root "bootloader\patches\msv3-1.0.14-buffer-identity-trace.patch"
    # A DIAGNOSTIC release, and the one that should end this. 1.0.13 cleared the transmit path
    # (every frame acknowledged, no FIFO stall, no sequence loss, no pool exhaustion) and a
    # host-side capture cleared the host (every frame built correctly, correct little-endian type
    # bytes, valid headers and CRCs). Since the host computes each CRC over the same bytes it
    # sends, a frame that validates on the device cannot be wrong -- yet 15 of 16 arrive at
    # data_queue_element_alloc() as type 0x00. This records, for one good frame and for the first
    # rejected one, WHICH pool slot the SLIP layer filled against WHICH pool slot rx_extract()
    # returned. It adds two read-only accessors to hci_mem_pool.c and changes no pool behaviour.
    Sentinel = "MSV3_BUFFER_IDENTITY"
    SentinelIn = "lib\sdk\components\libraries\hci\hci_transport.c"
  }
}
if ([version]$ReleaseVersion -eq [version]"1.0.15") {
  # 1.0.15 IS WITHHELD. It identifies the root cause correctly and its fix is the WRONG SHAPE.
  #
  # The cause is real and measured: one hci_mem_pool serves both DFU transports, and since 1.0.4
  # started both, dfu_transport_ble_update_start()'s hci_mem_pool_open() resets the pool AFTER the
  # serial transport has produced a buffer and registered it with the SLIP layer. All nine values
  # in the 1.0.14 trace matched that replay exactly.
  #
  # But "open the pool once" is the wrong abstraction, and it would BREAK BLE RECOVERY -- the only
  # way back onto a probe whose SWD pads are coated. That second open is not purely destructive:
  # it is also what DISCARDS the serial transport's outstanding reservation and hands BLE a clean
  # pool, which is precisely why BLE OTA has worked all along. Suppress it and the BLE path
  # inherits serial's produced-but-unfilled slot 0, so BLE's first packet produces slot 1, sets
  # slot 1's length, and extracts slot 0 -- the identical failure with the transports reversed.
  # That is not a theory to test on hardware: it is the recovery channel.
  #
  # The correct invariant is EXCLUSIVE OWNERSHIP, not single initialisation. Exactly one transport
  # may own the pool for a session; it claims ownership at first ingress (BLE at its first DFU data
  # write, serial before the first CDC byte is drained), the claiming transport initialises the
  # pool for itself, and the loser must never reach rx_produce/rx_data_size_set/rx_extract/
  # rx_consume at all. A .bss owner byte cleared by the system reset is enough, provided every
  # serial failure path either keeps serial ownership or forces a reset before BLE is allowed.
  throw @"
MSV3 bootloader 1.0.15 is WITHHELD and must not be built or installed.
Its diagnosis is correct and its fix is not: suppressing the second hci_mem_pool_open() hands the
BLE transport the serial transport's stale reservation and reproduces the same buffer-identity
failure on the BLE path -- which is the only recovery channel on a coated probe.
Implement exclusive pool ownership instead (claim at first ingress, initialise for the winner, and
keep the loser out of the pool entirely). See
MSV3/docs/MSV3_USB_BLE_COEXISTENCE_FIRST_HARDWARE_2026-09-20.md.
"@
}
if ([version]$ReleaseVersion -ge [version]"1.0.16") {
  $patches += @{
    File = Join-Path $msv3Root "bootloader\patches\msv3-1.0.16-pool-ownership.patch"
    # THE WIRED DFU FIX. One hci_mem_pool serves BOTH transports; stock only ever started one, and
    # 1.0.4 started both, so dfu_transport_ble_update_start()'s hci_mem_pool_open() reset the pool
    # after the serial transport had produced a buffer and registered it with SLIP. read_index then
    # ran one ahead of the buffer SLIP was filling and every extract after the first returned a
    # produced-but-never-written buffer whose first byte reads as INVALID_PACKET. Confirmed on
    # probe 8: all nine values in the 1.0.14 trace matched the replay exactly.
    #
    # The fix is EXCLUSIVE OWNERSHIP, not single initialisation -- see the withheld 1.0.15 above for
    # why the obvious version would have broken BLE recovery. Also carries the fail-closed guard in
    # hci_transport_rx_pkt_extract(), so a future desync is refused rather than turned into corrupt
    # frame data. A silent failure to apply restores a bootloader that accepts START, erases the
    # application, and can never finish a wired transfer.
    Sentinel = "MSV3_POOL_OWNER"
    SentinelIn = "lib\sdk\components\libraries\hci\hci_mem_pool.c"
  }
}

if ([version]$ReleaseVersion -ge [version]"1.0.17") {
  $patches += @{
    File = Join-Path $msv3Root "bootloader\patches\msv3-1.0.17-init-ext-bounds.patch"
    # Bounds the extended-init-packet copy. dfu_init_prevalidate() derived the length by pointer
    # subtraction from two host-supplied values and memcpy()d it into m_extended_packet[104] with
    # no upper bound, BEFORE any signature was verified, with the uint8_t length variable declared
    # immediately after the array. The stock adafruit-nrfutil serial transport appends two padding
    # bytes to the init packet, so this was reached on every ordinary wired update -- and it is why
    # the wired path reset the device during the init packet once 1.0.16 let frames through at all.
    # The BLE transport writes the init packet verbatim, which is the other half of why BLE worked.
    # Accepted inputs and the signed span are unchanged; padding is ignored rather than trusted.
    Sentinel = "MSV3_INIT_EXT_BOUNDS"
    SentinelIn = "src\dfu_init.c"
  }
}




if ([version]$ReleaseVersion -ge [version]"1.0.18" -and
    [version]$ReleaseVersion -le [version]"1.0.20") {
  # 1.0.18, 1.0.19 and 1.0.20 are WITHDRAWN. They deferred the init packet and retried it while
  # an ASYNCHRONOUS application-bank erase finished -- machinery that exists only to survive an
  # asynchrony the stock bootloader does not have. Stock never enables the SoftDevice for a wired
  # session, so its flash path is synchronous and the erase completes before dfu_start_pkt_handle()
  # returns; the race cannot occur. 1.0.21 restores that configuration instead. The patches are
  # kept only as the record of three attempts at the wrong shape.
  throw @"
MSV3 bootloader 1.0.18-1.0.20 are WITHDRAWN and must not be built or installed.
They work around an asynchronous erase that a wired session should never have been running in the
first place. Build 1.0.21, which restores the stock no-SoftDevice wired configuration.
See MSV3/docs/MSV3_USB_BLE_COEXISTENCE_FIRST_HARDWARE_2026-09-20.md.
"@
}
if ([version]$ReleaseVersion -ge [version]"1.0.24") {
  $patches += @{
    File = Join-Path $msv3Root "bootloader\patches\msv3-1.0.24-serial-stock-mode.patch"
    # THE WIRED DFU FIX, and it is a restoration rather than an invention. Stock brings up ONE
    # transport: the wired branch never enables the SoftDevice, so flash is synchronous and the
    # application-bank erase finishes before dfu_start_pkt_handle() returns. 1.0.4 attached USB
    # inside the SoftDevice-enabled branch, which made wired DFU asynchronous and caused both
    # confirmed defects. A serial session now restores the stock shape -- close BLE, disable the
    # SoftDevice, clear _ota_dfu, in that order and only after the disable succeeds -- and arms a
    # lease so that losing VBUS or going quiet forces a reset. 1.0.22 adds the watchdog feed the
    # blocking erase needs: 33 pages at ~85 ms is ~2.8 s with the CPU fully blocked, and nothing
    # else in the image reloads the WDT across it. That lease is the safety property:
    # without it a pulled cable leaves the board wired-only with no SoftDevice and no way back, on
    # hardware with no SWD. A silent failure to apply restores the reboot-on-init behaviour.
    Sentinel = "MSV3_SERIAL_STOCK_MODE"
    SentinelIn = "src\main.c"
  }
}

foreach ($p in $patches) {
  if (!(Test-Path -LiteralPath $p.File)) {
    throw "Required bootloader patch is missing: $($p.File)"
  }
  & git -C $source apply --whitespace=nowarn $p.File
  if ($LASTEXITCODE -ne 0) { throw "Failed to apply bootloader patch $($p.File)" }

  $sentinelPath = Join-Path $source $p.SentinelIn
  if (!(Select-String -LiteralPath $sentinelPath -SimpleMatch -Pattern $p.Sentinel -Quiet)) {
    throw "Patch $([IO.Path]::GetFileName($p.File)) reported success but its sentinel '$($p.Sentinel)' is absent from $sentinelPath. Refusing to build a bootloader whose provenance cannot be confirmed."
  }
  Write-Host "MSV3 bootloader patch applied and verified: $([IO.Path]::GetFileName($p.File))"
}

if (Test-Path -LiteralPath $boardDest) { Remove-Item -LiteralPath $boardDest -Recurse -Force }
Copy-Item -LiteralPath $boardSource -Destination $boardDest -Recurse

# Use an MSV3-only legacy DFU device type. The stock image accepts generic 0x0052 packages;
# leaving that value in a sealed product would permit a correctly signed but wrong-family image.
$dfuInit = Join-Path $source "src\dfu_init.c"
$dfuText = [IO.File]::ReadAllText($dfuInit)
$stockDeviceType = '#define ADAFRUIT_DEVICE_TYPE         0x0052'
$msv3DeviceType = '#define ADAFRUIT_DEVICE_TYPE         0x3356'
if ($dfuText.Contains($stockDeviceType)) {
  $dfuText = $dfuText.Replace($stockDeviceType, $msv3DeviceType)
  [IO.File]::WriteAllText($dfuInit, $dfuText, [Text.UTF8Encoding]::new($false))
} elseif (!$dfuText.Contains($msv3DeviceType)) {
  throw "Refusing to guess while setting the MSV3 DFU device type"
}

$make = Join-Path $env:LOCALAPPDATA `
  "Microsoft\WinGet\Packages\ezwinports.make_Microsoft.Winget.Source_8wekyb3d8bbwe\bin\make.exe"
if (!(Test-Path -LiteralPath $make)) {
  throw "GNU Make 4.4.1 is required. Install WinGet package ezwinports.make."
}
$gcc = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA "Arduino15\packages\adafruit\tools\arm-none-eabi-gcc") `
  -Filter "arm-none-eabi-gcc.exe" -Recurse | Select-Object -First 1
if (!$gcc) { throw "Arduino's arm-none-eabi-gcc toolchain was not found" }
$crossCompile = ((Split-Path -Parent $gcc.FullName) -replace '\\', '/') + '/arm-none-eabi-'

$bootVersion = "0.11.0-msv3.$ReleaseVersion"
$makeArgs = @(
  "BOARD=msv3_e73",
  "GIT_VERSION=$bootVersion",
  # GNU sed is not part of the minimal Windows make package. These are the exact values that
  # upstream's sed expression would derive from the pinned bootloader tag 0.11.0.
  "_VER_MAJ=0",
  "_VER_MIN=11",
  "_VER_PAT=0",
  "_VER3=0 11 0",
  "CROSS_COMPILE=$crossCompile",
  "NRFUTIL=$($nrfutil -replace '\\', '/')",
  "SIGNED_FW=1",
  "SIGNED_FW_QX=$qx",
  "SIGNED_FW_QY=$qy",
  # The 1.0.0 dual-bank build accepted a complete BLE transfer but failed to leave a valid
  # application in bank 0 on the real E73 board. Adafruit's default single-bank path has been
  # proven on this hardware and DEFAULT_TO_OTA_DFU still guarantees recovery after an invalid app.
  "DUALBANK_FW=0",
  "DEFAULT_TO_OTA_DFU=1",
  "all"
)
$priorBuild = Join-Path $source "_build\build-msv3_e73"
if (Test-Path -LiteralPath $priorBuild) {
  Remove-Item -LiteralPath $priorBuild -Recurse -Force
}
Push-Location $source
try {
  & $make @makeArgs
  if ($LASTEXITCODE -ne 0) { throw "MSV3 bootloader build failed" }
} finally {
  Pop-Location
}

$buildDir = Join-Path $source "_build\build-msv3_e73"
$bootOnly = Join-Path $buildDir "msv3_e73_bootloader-$bootVersion.hex"
$bootMerged = Join-Path $buildDir "msv3_e73_bootloader-$($bootVersion)_s140_6.1.1.hex"
if (!(Test-Path -LiteralPath $bootOnly) -or !(Test-Path -LiteralPath $bootMerged)) {
  throw "Expected bootloader outputs were not created"
}

if ([version]$ReleaseVersion -ge [version]"1.0.3") {
  # Verify the linked artifact, not only the patched source. This is the other half of the
  # application/bootloader RAM ABI: the bootloader stack must end immediately below the retained
  # guard word or a watchdog reset can erase the count before the application reads it.
  $bootMap = Join-Path $buildDir "msv3_e73_bootloader-$bootVersion.out.map"
  if (!(Test-Path -LiteralPath $bootMap)) {
    throw "Bootloader 1.0.3+ build map is missing: $bootMap"
  }
  $bootMapText = [IO.File]::ReadAllText($bootMap)

  # The guard word's address is an ABI with the application linker and never moves. What DOES move
  # is where the stack has to stop, because 1.0.6 inserts the 64-byte DFU trace underneath the
  # guard. Check the guard region and the stack top separately, so neither release can quietly
  # acquire the other's address.
  if ($bootMapText -notmatch '(?m)^MSV3_BOOT_GUARD_RETAINED\s+0x2003fff8\s+0x00000008\b') {
    throw @"
Bootloader 1.0.3+ did not link the retained guard word where the application expects it:
  $bootMap
Expected an 8-byte MSV3_BOOT_GUARD_RETAINED region at 0x2003FFF8.
"@
  }

  if ([version]$ReleaseVersion -ge [version]"1.0.6") {
    # 1.0.6 retains a DFU decision record between the stack and the guard word. It is reserved by
    # EXCLUSION from the RAM region rather than by a section, so the map is the only place that can
    # prove the stack actually stops below it. Without this the record would be silently
    # overwritten by the bootloader's own stack and every field it reports would be fiction --
    # which is worse than having no instrumentation at all.
    #
    # 1.0.13 grew the record from 64 to 96 bytes for the TX-stall counters. The guard word above it
    # is an application ABI and cannot move, so the base moved DOWN instead, taking 32 bytes off
    # the bootloader stack. Both geometries are checked, and a build must match its own: a 1.0.13
    # image linked at the old base would put its new counters underneath the stack.
    if ([version]$ReleaseVersion -ge [version]"1.0.13") {
      $traceBase = '0x2003ff98'; $traceLen = '0x00000060'; $traceBytes = 96
    } else {
      $traceBase = '0x2003ffb8'; $traceLen = '0x00000040'; $traceBytes = 64
    }
    if ($bootMapText -notmatch ('(?m)^MSV3_DFU_TRACE\s+' + $traceBase + '\s+' + $traceLen + '\b') -or
        $bootMapText -notmatch ('(?m)^\s*' + $traceBase + '\s+__StackTop\b')) {
      throw @"
Bootloader 1.0.6+ did not link with the DFU trace retention:
  $bootMap
Expected a $traceBytes-byte MSV3_DFU_TRACE region at $traceBase and __StackTop at $traceBase, with
MSV3_BOOT_GUARD_RETAINED still at 0x2003FFF8.
"@
    }
    Write-Host "MSV3 bootloader map check: OK (stack ends at $traceBase; $traceBytes-byte DFU trace, then the guard word)"
  }
  elseif ($bootMapText -notmatch '(?m)^\s*0x2003fff8\s+__StackTop\b') {
    throw @"
Bootloader 1.0.3+ did not link with the required retained-RAM ABI:
  $bootMap
Expected __StackTop at 0x2003FFF8.
"@
  }
  else {
    Write-Host "MSV3 bootloader map check: OK (stack ends below retained state at 0x2003FFF8)"
  }
}

if ([version]$ReleaseVersion -ge [version]"1.0.4") {
  # Again, verify the linked artifact rather than only the patched source. The coexistence helper
  # is static and reachable only from check_dfu_mode(), so -ffunction-sections/--gc-sections would
  # be entirely within its rights to discard it if the call site ever got compiled out by the
  # NRF_USBD/MSV3_USB_BLE_COEXIST guards. The sentinel grep above cannot see that: the #define
  # would still be sitting in the source it just patched. A bootloader that silently linked without
  # the wired path is the exact artifact this release exists to avoid shipping.
  $bootMap = Join-Path $buildDir "msv3_e73_bootloader-$bootVersion.out.map"
  if (!(Test-Path -LiteralPath $bootMap)) {
    throw "Bootloader 1.0.4+ build map is missing: $bootMap"
  }
  $bootMapText104 = [IO.File]::ReadAllText($bootMap)
  if ($bootMapText104 -notmatch 'msv3_usb_attach_if_vbus') {
    throw @"
Bootloader 1.0.4+ linked without the USB/BLE coexistence helper:
  $bootMap
Expected msv3_usb_attach_if_vbus in the link map. The patch applied but the function was not linked,
so this bootloader would fall back to BLE-only recovery with no wired escape.
"@
  }
  if ($bootMapText104 -notmatch 'dfu_transport_claim') {
    throw @"
Bootloader 1.0.4+ linked without the DFU transport arbiter:
  $bootMap
Expected dfu_transport_claim in the link map. Without it both transports could write the same
single bank, which erases the application before Init authentication and leaves neither image
recoverable.
"@
  }
  Write-Host "MSV3 bootloader map check: OK (USB/BLE coexistence + transport arbiter linked)"
}

if (!$SkipApplicationCompile) {
  if ($SensorId -lt 0) {
    throw "-SensorId is required to build a release application (the sketch takes its identity as a build parameter). Pass -SkipApplicationCompile to build only the bootloader."
  }
  & powershell -ExecutionPolicy Bypass -File (Join-Path $scriptDir "arduino_msv3_probe.ps1") `
    -Action doctor
  if ($LASTEXITCODE -ne 0) { throw "MSV3 doctor check failed" }
  $probeArgs = @("-Action", "compile", "-Sketch", "arduino/MSV3_Moisture_BLE_Sleep",
                 "-SensorId", $SensorId,
                 "-FdcSettleCount", $FdcSettleCount,
                 "-WakeDiscardConversions", $WakeDiscardConversions,
                 "-FdcIdriveCode", $FdcIdriveCode,
                 "-FdcDeglitchMhz", $FdcDeglitchMhz)
  if ($BuildTag) { $probeArgs += @("-BuildTag", $BuildTag) }
  # Forward -SerialLogs. It used to be absent from this script's param block entirely, so passing
  # it produced three bench packages with logging OFF and no error at all -- and the guard-2
  # watchdog-hang test's whole evidence is reading WATCHDOG on successive resets over serial.
  # A build flag that is silently dropped is worse than one that is unsupported.
  if ($SerialLogs) { $probeArgs += "-SerialLogs" }
  if ($BootGuard) { $probeArgs += "-BootGuard" }
  if ($HangTest) { $probeArgs += "-HangTest" }
  if ($PSBoundParameters.ContainsKey('HybridMuxCode') -and -not $HybridV12Tank) {
    throw "-HybridMuxCode only applies with -HybridV12Tank; on its own it would be silently ignored."
  }
  if ($HybridV12Tank) { $probeArgs += @("-HybridV12Tank", "-HybridMuxCode", $HybridMuxCode) }
  & powershell -ExecutionPolicy Bypass -File (Join-Path $scriptDir "arduino_msv3_probe.ps1") @probeArgs
  if ($LASTEXITCODE -ne 0) { throw "MSV3 production application build failed" }
}

# -SkipApplicationCompile NEVER PRODUCES AN APPLICATION PACKAGE. It builds the bootloader only.
#
# It used to package whatever hex was sitting in the shared build cache. Demonstrated 2026-09-06:
# `-ReleaseVersion 1.0.1 -ApplicationVersion 65537 -SkipApplicationCompile` emitted a correctly
# signed package NAMED and VERSIONED 1.0.1/65537 whose image was actually MSV3-MOISTURE-004 build
# 2026-09-06-s4v50. Nothing downstream would have questioned it: valid signature, right device
# type, only the identity disagreed with the filename.
#
# An identity check against -SensorId was the first fix, and it is still in place below -- but it
# is not sufficient on its own, because it cannot see SAME-SENSOR staleness. A cached sensor-004
# image built from older source, repackaged at a new version, presents a correct identity AND a
# correct signed version while being the wrong code. That is precisely how probe 4's running
# firmware became unreproducible in the first place, so the workflow that allows it is removed
# rather than guarded.
#
# If reusing a prebuilt application is ever wanted, add an explicit -ApplicationHex parameter with
# a required expected hash. Do not restore implicit consumption of shared cache state.
if ($SkipApplicationCompile) {
  Write-Host ""
  Write-Host "-SkipApplicationCompile: bootloader only. No application or migration package will be"
  Write-Host "produced, because the only image available would be unattributable build-cache state."
  $releaseDirBootOnly = Join-Path $msv3Root "bootloader\generated\$ReleaseVersion"
  New-Item -ItemType Directory -Force -Path $releaseDirBootOnly | Out-Null
  $releaseBootOnly = Join-Path $releaseDirBootOnly "msv3-e73-recovery-signed-s140-6.1.1.hex"
  Copy-Item -LiteralPath $bootMerged -Destination $releaseBootOnly -Force
  $bootOnlyHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $releaseBootOnly).Hash.ToLowerInvariant()
  Write-Host ""
  Write-Host "MSV3 bootloader built: $releaseBootOnly"
  Write-Host "  sha256 $bootOnlyHash"
  Write-Host "Release manifest NOT rewritten -- it describes a full release and this was not one."
  exit 0
}

$appHex = Join-Path $cacheRoot "msv3-probe\MSV3_Moisture_BLE_Sleep.ino.hex"
if (!(Test-Path -LiteralPath $appHex)) { throw "Missing production application: $appHex" }

$releaseDir = Join-Path $msv3Root "bootloader\generated\$ReleaseVersion"
$canonicalDir = Join-Path $msv3Root "bootloader\generated"
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null
$releaseBoot = Join-Path $releaseDir "msv3-e73-recovery-signed-s140-6.1.1.hex"
# Every newly built per-probe APPLICATION package includes its application version in the name.
# This was originally limited to bootloader release 1.0.2+, but probe 4 still runs 1.0.1 and needs
# several controlled application-only experiments without replacing its bootloader. The older
# unversioned artifacts stay untouched; this rule is additive and gives every new image a new path.
# Anti-rollback and settling tests both need N-1, N and N+1 to coexist rather than differ only in a
# signed init-packet field that is invisible in the filename.
$otaName = if ($SensorId -lt 0) {
  "msv3-$ReleaseVersion-signed-ota.zip"
} else {
  "msv3-$ReleaseVersion-sensor$('{0:d3}' -f $SensorId)-v$ApplicationVersion-signed-ota.zip"
}
$signedOta = Join-Path $releaseDir $otaName
# The migration package carries SoftDevice + bootloader + APPLICATION, so it embeds a sensor
# identity and application version exactly like the OTA package does. Older builders omitted one
# or both from this filename, so another application build in the same bootloader release collided
# even though it contained different signed bytes.
# Flashing the wrong one gives two probes the same sensor id and silently mixes two plants' data
# in the receiver -- which has already happened once on this project, with identity 005.
$initialInstallName = if ($SensorId -ge 0) {
  "msv3-$ReleaseVersion-sensor$('{0:d3}' -f $SensorId)-v$ApplicationVersion-initial-install-from-stock.zip"
} else {
  "msv3-$ReleaseVersion-initial-install-from-stock.zip"
}
$initialInstall = Join-Path $releaseDir $initialInstallName

# A DFU package is NOT byte-reproducible -- the ZIP carries timestamps and the ECDSA signature
# uses a fresh nonce -- so "rebuild and compare bytes" cannot be the guard here. The invariant
# that actually matters is weaker and enforceable: a released artifact path is written once.
# WRITE-ONCE, with no override. There deliberately is no -Force switch here: "write-once unless
# you pass a flag" is not write-once, and the flag would be reached for in exactly the situation
# it exists to prevent. If a path needs different bytes, it needs a different name -- bump
# -ApplicationVersion, which from 1.0.2 is part of the filename, or cut a new -ReleaseVersion for
# the bootloader hex. Superseding an artifact is then additive and the old one survives intact.
function Assert-NoSilentOverwrite {
  param(
    [string]$Path,
    [string]$What,
    # For an artifact that IS byte-reproducible, the file about to be written. Rewriting identical
    # bytes is a no-op, not a replacement, and refusing it would block every later application
    # build inside an existing release directory -- the bootloader hex is rebuilt on every run.
    [string]$CandidatePath
  )
  # $true means "nothing is there, go ahead and write"; $false means "identical bytes already
  # present, skip the write". A throw means the path is taken by different bytes.
  if (!(Test-Path -LiteralPath $Path)) { return $true }

  if ($CandidatePath -and (Test-Path -LiteralPath $CandidatePath)) {
    $existingHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
    $candidateHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $CandidatePath).Hash
    if ($existingHash -eq $candidateHash) {
      # Returns $false so the caller SKIPS the write entirely rather than being cleared to
      # overwrite. Permitting the copy would leave a check/write window in which the artifact
      # could change between the hash comparison and the copy -- and "write-once" has to mean the
      # bytes are never rewritten, not that rewriting them was approved.
      Write-Host "$What is unchanged (sha256 $($existingHash.ToLowerInvariant().Substring(0,16))...); leaving it untouched"
      return $false
    }
  }

  throw @"
Refusing to overwrite an existing $What :
  $Path
This exact path was already released, and a DFU package is not byte-reproducible (ZIP timestamps,
fresh ECDSA nonce), so rebuilding it can only ever make a (name, version) pair stop naming one
image. That is how the 1.0.2 archive lost the provenance of probe 4's application -- see
MSV3/bootloader/generated/1.0.2/DISPUTED.json.
The resolution is always ADDITIVE: bump -ApplicationVersion so this build gets its own filename
(or cut a new -ReleaseVersion for the bootloader hex) and leave the existing artifact where it is.
Retiring the old one is a separate, reviewable commit -- record it in that directory's
DISPUTED.json. Do NOT move it aside and reuse its pathname: that still ends with two different
byte sequences having borne the same release identity, which is the whole failure being prevented.
"@
}

$null = Assert-NoSilentOverwrite -Path $signedOta -What "signed OTA package"
$null = Assert-NoSilentOverwrite -Path $initialInstall -What "initial-install package"
$bootHexNeedsWrite = Assert-NoSilentOverwrite -Path $releaseBoot -What "release bootloader hex" `
                       -CandidatePath $bootMerged

if ($bootHexNeedsWrite) { Copy-Item -LiteralPath $bootMerged -Destination $releaseBoot -Force }

& $nrfutil dfu genpkg --dev-type 0x3356 --dev-revision 52840 `
  --application-version $ApplicationVersion --sd-req 0xB6 `
  --application $appHex --key-file $keyPath $signedOta
if ($LASTEXITCODE -ne 0) { throw "Signed OTA package generation failed" }

# One-time bridge: the installed generic stock loader requires 0x0052 and CRC16 init data.
& $nrfutil dfu genpkg --dev-type 0x0052 --dev-revision 52840 `
  --application-version $ApplicationVersion --sd-req 0xB6 `
  --application $appHex --bootloader $bootOnly --softdevice $requiredSource $initialInstall
if ($LASTEXITCODE -ne 0) { throw "Initial stock-to-MSV3 package generation failed" }

# SIGNED SD+BL+APP, for delivering a BOOTLOADER to a probe that is ALREADY on MSV3's own 0x3356.
#
# The initial-install package above is the same three components, but it is deliberately unsigned
# and tagged 0x0052 because the generic stock loader cannot parse a signed extended init packet.
# Once a board is migrated, that package is useless to it and the archive held nothing else that
# could carry a bootloader -- which is why a bootloader update was described as needing SWD. It
# does not; it needed this artifact.
#
# It carries the APPLICATION as well as SD+BL on purpose. Every wrong-target guard binds a package
# to a probe through the MSV3-MOISTURE-NNN string inside the application image, so a
# bootloader-only package would have no identity for msv3_fleet.py to check and would defeat the
# registry. Shipping all three keeps the binding, at the cost of also replacing the application --
# which is the safer trade, because the application is the component that CAN be re-sent over BLE.
#
# Staging is interruptible-safe (an aborted transfer leaves an invalid application, which
# DEFAULT_TO_OTA_DFU turns into persistent BLE recovery). The bootloader swap itself is performed
# by the MBR on the next boot and journals through the MBR params page, so it resumes after a
# power cut -- PROVIDED UICR.NRFFW[1] is provisioned on that board. ble_ota_update.py enforces
# that per-probe via mbr_params_page_verified; see MSV3/fleet.json.
if ($BootloaderOta) {
  if ($SensorId -lt 0) {
    throw "-BootloaderOta needs -SensorId: the application half is what binds the package to a probe."
  }
  $blOtaName = "msv3-$ReleaseVersion-sensor$('{0:d3}' -f $SensorId)-v$ApplicationVersion-sdbl-app-signed-ota.zip"
  $blOta = Join-Path $releaseDir $blOtaName
  $null = Assert-NoSilentOverwrite -Path $blOta -What "signed SD+BL+APP package"
  & $nrfutil dfu genpkg --dev-type 0x3356 --dev-revision 52840 `
    --application-version $ApplicationVersion --sd-req 0xB6 `
    --application $appHex --bootloader $bootOnly --softdevice $requiredSource `
    --key-file $keyPath $blOta
  if ($LASTEXITCODE -ne 0) { throw "Signed SD+BL+APP package generation failed" }

  & $pythonExe (Join-Path $scriptDir "verify_msv3_dfu_package.py") $blOta `
    --device-type 0x3356 --application-version $ApplicationVersion --key $keyPath
  if ($LASTEXITCODE -ne 0) { throw "Signed SD+BL+APP package verification failed" }

  $blOtaHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $blOta).Hash.ToLowerInvariant()
  Write-Host ""
  Write-Host "Signed SD+BL+APP package: $blOta"
  Write-Host "  sha256=$blOtaHash"
  Write-Host "  Carries bootloader $ReleaseVersion. Send with:"
  Write-Host "    ble_ota_update.py --probe $SensorId <pkg> --bootloader"
  Write-Host "  That flag additionally requires the probe's mbr_params_page_verified to be set."
}

& $pythonExe (Join-Path $scriptDir "verify_msv3_dfu_package.py") $signedOta `
  --device-type 0x3356 --application-version $ApplicationVersion --key $keyPath
if ($LASTEXITCODE -ne 0) { throw "Signed OTA package verification failed" }

# Confirm the package that was just SIGNED actually carries the identity this run asked for.
#
# -SkipApplicationCompile packages whatever is sitting in the shared build cache. Demonstrated
# 2026-09-06 while regression-testing this script: a `-ReleaseVersion 1.0.1 -ApplicationVersion
# 65537 -SkipApplicationCompile` run produced a package NAMED and VERSIONED 1.0.1/65537 whose image
# was really MSV3-MOISTURE-004 build 2026-09-06-s4v50. It was correctly signed, so nothing
# downstream would have questioned it -- and a mislabelled signed package is exactly what the fleet
# registry exists to prevent. Once a probe is coated the only remedy for a wrong install is another
# OTA, so this is checked at the point of signing, not at the point of flashing.
if ($SensorId -ge 0) {
  & $pythonExe -c @"
import sys
from pathlib import Path
sys.path.insert(0, r'$repoRoot\scripts')
import msv3_fleet
pkg = msv3_fleet.read_package(Path(sys.argv[1]))
want = int(sys.argv[2])
if pkg.sensor_id != want:
    sys.exit('IDENTITY MISMATCH: package carries MSV3-MOISTURE-%03d but -SensorId %d was requested.\n'
             'The build cache is from a different build. Drop -SkipApplicationCompile, or pass the\n'
             '-SensorId matching the image you intend to ship.' % (pkg.sensor_id, want))
print('MSV3 package identity check: OK (MSV3-MOISTURE-%03d, version %d)'
      % (pkg.sensor_id, pkg.application_version))
"@ $signedOta $SensorId
  if ($LASTEXITCODE -ne 0) {
    # Delete the offending artifacts before throwing. Detecting a mislabelled signed package and
    # then leaving it on disk is barely better than not detecting it: it is correctly signed, its
    # filename asserts an identity it does not have, and the next person to reach into this
    # directory has no reason to distrust it. Found the hard way -- the first version of this
    # check reported the mismatch and left the package sitting there.
    foreach ($stale in @($signedOta, $initialInstall)) {
      if (Test-Path -LiteralPath $stale) {
        Remove-Item -LiteralPath $stale -Force
        Write-Host "Removed mislabelled package: $([IO.Path]::GetFileName($stale))"
      }
    }
    throw "Signed package identity does not match -SensorId $SensorId (mislabelled artifacts deleted)"
  }
}
& $pythonExe (Join-Path $scriptDir "verify_msv3_dfu_package.py") $initialInstall `
  --device-type 0x0052 --application-version $ApplicationVersion
if ($LASTEXITCODE -ne 0) { throw "Initial migration package verification failed" }

$canonicalBoot = Join-Path $canonicalDir "msv3-e73-recovery-signed-s140-6.1.1.hex"
$newerRelease = Get-ChildItem -LiteralPath $canonicalDir -Directory | Where-Object {
  $_.Name -match '^\d+\.\d+\.\d+$' -and [version]$_.Name -gt [version]$ReleaseVersion
} | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if ($null -eq $newerRelease) {
  # This unversioned alias is the recovery image a human will reach for first. Only the newest
  # release may own it; rebuilding an older probe-specific application must never silently roll
  # the canonical rescue bootloader backward.
  Copy-Item -LiteralPath $releaseBoot -Destination $canonicalBoot -Force
  $bootHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $canonicalBoot).Hash.ToLowerInvariant()
  [IO.File]::WriteAllText("$canonicalBoot.sha256", "$bootHash  $([IO.Path]::GetFileName($canonicalBoot))`n", [Text.UTF8Encoding]::new($false))
} else {
  Write-Host "Canonical recovery alias left at newer release $($newerRelease.Name); historical $ReleaseVersion build cannot replace it"
}

$vkHex = (& $nrfutil keys --show-vk hex $keyPath | Out-String).Trim()
$files = @($releaseBoot, $signedOta, $initialInstall, $appHex) | ForEach-Object {
  $entry = [ordered]@{
    name = [IO.Path]::GetFileName($_)
    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_).Hash.ToLowerInvariant()
    bytes = (Get-Item -LiteralPath $_).Length
  }
  # $appHex lives in the gitignored build cache and is never copied into the release directory.
  # Recording its hash is worth doing -- it ties a release to the exact application hex behind its
  # packages -- but the archive verifier must not then hunt for a file that was never archived.
  if ($_ -eq $appHex) {
    $entry.provenance_only = $true
    $entry.note = "hash of the application hex this release was built from; it lives in the gitignored build cache and is deliberately not archived here"
  }
  $entry
}
$manifest = [ordered]@{
  release = $ReleaseVersion
  application_version = $ApplicationVersion
  device_type = "0x3356"
  softdevice_requirement = "0x00B6 (S140 6.1.1)"
  application_configuration = [ordered]@{
    fdc_settle_count = $FdcSettleCount
    wake_discard_conversions = $WakeDiscardConversions
    fdc_idrive_code = $FdcIdriveCode
    fdc_deglitch_mhz = $FdcDeglitchMhz
  }
  bootloader = [ordered]@{
    upstream = "Adafruit_nRF52_Bootloader 0.11.0"
    dual_bank = $false
    default_to_ble_recovery = $true
    signed_dfu_required = $true
    uf2_enabled = $false
  }
  public_verification_key = $vkHex
  files = $files
}
$manifestPath = Join-Path $releaseDir "release-manifest.json"

# The `files` array belongs to the DIRECTORY, not to this build. It used to be rebuilt from only
# the four files of the current run and written over the top, so a release directory's manifest
# recorded nothing but its most recent build -- which is why 1.0.3 listed sensor008 alone while
# probes 5, 6 and 7 had been flashed from packages sitting unrecorded beside it.
#
# So: preserve whatever is already recorded, and let verify_msv3_release_archive.py be the single
# writer of that array. It merges in what is on disk and REFUSES to rewrite a name whose bytes
# changed, which is a rule this script cannot enforce on its own from inside one build.
if (Test-Path -LiteralPath $manifestPath) {
  $existing = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
  if ($existing.PSObject.Properties.Name -contains "files") {
    $manifest.files = $existing.files
  }
}
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 6) + "`n", [Text.UTF8Encoding]::new($false))

# Record and then PROVE the archive, in that order. A release that cannot pass its own integrity
# check is not a release: these packages are the recovery path for probes whose SWD pads are
# about to disappear under conformal coating.
& $pythonExe (Join-Path $repoRoot "scripts\verify_msv3_release_archive.py") `
  --release $ReleaseVersion --sync-manifest --write-sums
if ($LASTEXITCODE -ne 0) { throw "Release archive record/verification failed for $ReleaseVersion" }

Write-Host "MSV3 release ready: $releaseDir"
Write-Host "  Future OTA: $signedOta"
Write-Host "  One-time stock migration: $initialInstall"
Write-Warning "Back up $keyPath offline before coating a production board."
