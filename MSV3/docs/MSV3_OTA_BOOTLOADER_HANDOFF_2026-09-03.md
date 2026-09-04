# MSV3 recoverable-bootloader handoff — 2026-09-03 (RESOLVED)

**Current state, superseding everything below.** BLE-only OTA works end to end: buttonless jump,
bootloader handoff, signed transfer, application boot — no USB, no SWD. Bootloader 1.0.1
(single-bank, `DEFAULT_TO_OTA_DFU`, signed, device type `0x3356`) is installed and confirmed via its
DIS string `0.11.0-msv3.1.0.1 s140 6.1.1`. The bench board runs the production application as
`MSV3-MOISTURE-099` (bench identity, built from a local edit; committed source stays on the fleet
identity). A ~131 KB update takes about 31 s wall clock, ~15 s of which is the transfer itself.

The sections below are kept in chronological order as the investigation record. **They contain
conclusions that were later disproven** — most importantly the "BLE is exhausted, SWD is the path"
finding, which was correct for the 1.0.0 dual-bank bootloader that was installed at the time and is
no longer true. Read the two 2026-09-03 night sections for what actually holds.

Flashing tool: `scripts/ble_ota_update.py` (repository root, not `MSV3/scripts/`).

## ⚠️ Read first: the private signing key is NOT in this repository

`MSV3/.secrets/msv3-dfu-signing-private.pem` is gitignored (`.gitignore:15`), correctly — it must
never be committed. **But that means cloning this repo elsewhere does not give you the ability to
sign packages for the bootloader now installed on the bench board.** That bootloader was built with
`SIGNED_FW=1` and only accepts images signed by that exact key.

Carry the key over separately (USB stick / password manager) or you cannot update that board at
all — not over BLE, not over USB. The only escape without it is reflashing a different bootloader
over SWD.

The matching public key is recorded in `MSV3/bootloader/generated/1.0.0/release-manifest.json`, so
you can always confirm whether a key you have is the right one.

## Physical state of the bench board

> **SUPERSEDED — this table describes the board mid-investigation on 2026-09-03 and is no longer
> true.** It is kept because the reasoning below refers to it. Current state: bootloader 1.0.1
> (single-bank), a valid application running as `MSV3-MOISTURE-099`, and BLE OTA working end to
> end. See the header and the two "night" sections.

| | |
|---|---|
| Application | **None.** Erased by the SoftDevice+bootloader migration and never successfully replaced |
| Bootloader | New signed / dual-bank / `DEFAULT_TO_OTA_DFU` build, installed and running |
| BLE identity | Advertising as `AdaDFU`, address `DB:AD:4D:9D:21:BB`, Nordic DFU service `00001530-...` |
| USB | **No CDC, and none available** — see the `DEFAULT_TO_OTA_DFU` note below |
| Risk | **Not bricked.** Re-flashable over BLE indefinitely |

The board is doing exactly what the new bootloader was built to do: no valid application, so it
sits in BLE recovery instead of dying. That property is now **proven live on real hardware**, which
was the entire point of the exercise.

## What is proven

- The bootloader's BLE recovery mode works with no application present and no USB.
- A signed application package transfers end to end: every DFU control-point status returned
  success, including Start DFU, Init Packet, Receive Firmware, Validate, and Activate.
- The ECDSA signature, device type `0x3356`, and SoftDevice requirement `0xB6` are all **accepted**
  — the client raises on a rejected init packet and did not.

## The open failure

After a successful transfer and activation the board reboots **back into `AdaDFU`**, meaning
`bootloader_app_is_valid()` returns false. The application is written and validated but is not
accepted at boot.

Ruled out:

- **GPREGRET / DFU-entry loop.** `src/main.c:232` re-arms `DFU_MAGIC_OTA_RESET` when a BLE client
  has connected, which looked like a likely loop. A full power cycle clears `GPREGRET`, and the
  board still came up `AdaDFU` — so `dfu_start` is false and `valid_app` is genuinely false.
- **Application size.** 130,780 bytes against a `DFU_IMAGE_MAX_SIZE_BANKED` of roughly 421 KB.
  Now also ruled out empirically -- see the 2026-09-03 evening size test below.
- **Signature / device type / SoftDevice requirement.** All accepted, as above.
- **Dropped packets.** No `PRN Timeout` warnings were logged during the transfer.

**Confirmed cause (see the size test below): `DUALBANK_FW=1`.** Adafruit ships single-bank by default; this build enables a
code path that is not their tested configuration. Earlier the same day, the same client and the
same PRN/chunk settings performed a flawless OTA against the *stock single-bank* bootloader, which
narrows the difference to the signed/dual-bank build.

Unexplained secondary observation: the transfer took **11 m 50 s** versus **3 m 31 s** for a
same-size image against the stock bootloader, with no PRN timeouts. Probably a slower negotiated
connection interval in the bootloader, but not confirmed.

## 2026-09-03 evening: size test closes the dual-bank question, and BLE is exhausted

The open hypothesis was that dual-bank bank-0 validation might be failing on a size or offset
miscalculation rather than being broken outright. Tested directly, at zero risk to the board.

- Compiled `arduino/MSV3_E73_LED_Test` through `arduino_msv3_probe.ps1` (LFCLK guard reported OK):
  **21,372 bytes**, about one sixth of the production application.
- Packaged it signed with the existing key, `--dev-type 0x3356 --dev-revision 52840 --sd-req 0xB6`,
  application version 65538. `verify_msv3_dfu_package.py` passed.
- Pushed it to `AdaDFU` at `DB:AD:4D:9D:21:BB` with `ble_dfu_recover.py`. The client reported
  `Sending sizes: SD=0 BL=0 App=21380 bytes`, then 100% transfer, Verify, Validate,
  `Activating & Resetting`, `DFU Complete.` -- every control-point status success, same as before.
- **The board came back advertising `AdaDFU`.** Confirmed by a 25-second scan immediately after.

`bootloader_app_is_valid()` therefore returns false for a 21 KB image just as it does for a 131 KB
one. Image size is not the variable. Dual-bank activation does not produce a bootable bank 0 on
this build at any size, which upgrades `DUALBANK_FW=1` from prime suspect to confirmed cause and
retires option 3 below -- there is nothing left to bisect from the package side.

**This also closes out BLE as a repair path for this board.** The bootloader itself has to be
replaced, and a bootloader self-update over BLE would be carried by the very activation code that
has now failed twice under observation. The standing "do not attempt a bootloader update over BLE"
rule below is not merely precautionary; on this specific build it would very likely fail, and
failure removes the only remaining wireless access. SWD is the path.

Unrelated but confirmed by the same scans: `MSV3-MOISTURE-004` and `MSV3-MOISTURE-005` are both
advertising normally on production firmware. Only the bench board is affected.

### The signing key is present and is the live trust root

The 2026-09-03 chat claim that the private key was missing from
`MSV3/.secrets/msv3-dfu-signing-private.pem` was wrong; the file is there. Verified rather than
assumed:

- `openssl ec -pubout` on that key derives Qx `b062ce05...a253bb94`, Qy `7264a851...89f59530`.
- Those exact 64 bytes are embedded in the **1.0.0** bootloader image -- the build running on the
  bench board -- at `0x000fbf74` and `0x000fbf94`, and in 1.0.1 at `0x000fbd18` / `0x000fbd38`.
- The same values appear in the committed 1.0.0 manifest, which `build_msv3_release.ps1` derives at
  build time via `nrfutil keys --show-vk hex`, not from any hardcoded string.
- The signed LED-test package above was produced and verified with that key and accepted by the
  board's bootloader.

A freshly generated random P-256 key cannot collide with one already burned into 1.0.0, so the key
on disk is the original. **1.0.1 keeps the same trust root** -- flashing it over SWD does not
establish a new one, and no key hunt or backup restore is required. The warning at the top of this
document is about the key being gitignored and therefore absent from a fresh clone, which remains
true and correct; it is not a statement that the key was lost. Back it up offline regardless:
`-GenerateSigningKey` would overwrite it, and after that the loss would be real.

## 2026-09-03 night: probe 4 migrated off the stock bootloader, USB -> BLE, on battery and solar

First migration of a **live fleet board**, and the first proof the whole path works outside the
bench. Probe 4 (`F2:16:32:1B:2B:47`, sensor 4, the water-immersion board) was on the stock
Adafruit bootloader with pre-OTA firmware: its advertisement read `flags=0x0e` — bit4 (OTA power
gate) clear at 3891 mV, well above the 3800 mV threshold, which is the signature of firmware that
has no OTA feature at all rather than a shut gate. A GATT connect confirmed it: **no DFU service,
only GAP and GATT.**

Sequence, ~5 minutes on USB power total:

1. `adafruit-nrfutil dfu serial` with the unsigned `0x0052` migration package (SoftDevice +
   bootloader + application, built for `SENSOR_ID = 4`). It reported a failure — but only at the
   *application* stage, having already written `SD_BL`. That is the expected handoff, not a fault:
   replacing the bootloader puts `DEFAULT_TO_OTA_DFU` in charge, which removes USB CDC, so the
   serial port disappears out from under the application stage. **USB carries the bootloader, BLE
   carries the application.**
2. The board appeared at `F2:16:32:1B:2B:48` as `AdaDFU` — address **+1**, the no-valid-app case,
   handled without change by the dual-address logic added earlier the same night.
3. `scripts/ble_ota_update.py --skip-jump` delivered the signed `0x3356` application over BLE.
   `DFU Complete`, 47 s, 2.75 kB/s at -74 dBm.
4. Verified booted: `MSV3-MOISTURE-004`, `sensor=4`, `wake=2`, `batt=3907mV`, **`flags=0x1e`** —
   bit4 now set, which only OTA-capable firmware sets. No `AdaDFU`, correct identity, no collision.

Probe 4 is now `0x3356` signed bootloader + `DEFAULT_TO_OTA_DFU` + OTA-capable application, and
updates over BLE alone from here — no cable, no SWD. Note the throughput difference from signal
alone: bench at -55 dBm managed 28 s / 4.8 kB/s, probe 4 in place at -74 dBm took 47 s / 2.75 kB/s,
with one control write (activate) taking 9.8 s. Same code, same parameters.

**One real bug surfaced on the live board.** `ReceiptGuard` aborted the first BLE attempt with a
false positive: an `asyncio.CancelledError` raised by a failed *connection* attempt was recorded as
a missed receipt, and that state carried into dfu_lib's retry, killing it at the Start DFU write
before a single packet went out. It failed safe — nothing written, board left advertising — but it
would have blocked every retry-after-a-flaky-connect. Guard state is now per-attempt, armed by the
Start DFU control write.

## 2026-09-03 night: transfer speed measured to its ceiling, and the flasher moved to the root

`scripts/ble_ota_update.py` (moved out of `MSV3/scripts/` -- it is the project's flashing tool, not
an MSV3 detail) gained `--profile`, which partitions a transfer's wall clock instead of guessing at
it. For a 134,432-byte image (551 image packets at 244 bytes):

| | writes | receipt waits | total |
|---|---|---|---|
| PRN 4 | 1.57s | 19.87s (n=137, p50 117ms) | 41.0s |
| **PRN 16** | 0.47s data + 1.02s control | **13.8s (n=34, p50 407ms)** | **31s** |
| PRN 32 | 1.45s | 18.48s (n=17, p50 995ms) | 38.6s |

**Per-packet receipt cost is flat at 26-31 ms regardless of PRN**, so the constraint is packet
delivery to the bootloader, not receipt round-trips -- PRN tuning has a floor and PRN 32 is worse
than 16 (longer stalls, same packet count). Data-packet writes total under half a second, so the
host stack is not the limit either.

**Practical transfer-phase ceiling on this bootloader/WinRT path: roughly 8-10 kB/s, about 15 s for
a 131 KB image.** Going faster needs more than one packet per connection event, which means shorter
connection intervals compiled into the bootloader, which needs SWD. Not worth it -- the transfer is
already the smaller half of the wall clock. Total run time varies 31-41 s, and that variance tracks
how long the client waits to catch the application's 5 s-per-60 s OTA advertising window. Trimming
`--connect-delay` and `--delay` changed nothing. This is not a measured connection-interval claim:
the PPPC characteristic was never re-read as raw bytes, and none is needed for the conclusion.

Two correctness fixes came out of the same pass:

- **`use_cached_services=False` on the transfer client.** Every transfer had been logging
  `Attempt 1 failed: E_CANCELED` and connecting only on dfu_lib's retry. After a buttonless jump
  the bootloader occupies the *same address* the application just had with an entirely different
  GATT table, and Windows serves its per-address service cache. Zero failed attempts since. (The
  same cache mechanism was tested and correctly ruled out for the jump *write* -- it applies only
  here, where one address hosts two different GATT tables.)
- **Packet-receipt desync now aborts.** Upstream dfu_lib logs `PRN Timeout, continuing anyway` and
  keeps streaming with flow control lost, which lets a late receipt satisfy the next window. At the
  old 20-byte chunk size that path produced eight timeouts and an image that failed Receive
  Firmware Image with status 6. dfu_lib is a pinned third-party checkout under an ignored
  directory, so `ReceiptGuard` in the flasher enforces the abort instead: a missed receipt marks
  the transfer desynced and the next packet write raises. The bootloader's CRC and signature are
  the last line of defence, not a licence to keep sending after desync.

## 2026-09-03 night: ROOT CAUSE of the buttonless jump, found by device-side instrumentation

Two independent bugs, one firmware and one client. Neither was guessable from source -- both were
found by recording checkpoints inside the authorize callback and printing them from the application
loop afterwards. Everything in the section below this one was real and worth fixing, but none of it
was the cause.

### Firmware: a SUCCESS authorize reply must set update=1

The probe's own log for a failing jump:

```
[DIAG] dfu authorize: checkpoint=8 op=1 conn=0 gattStatus=0x0 replyRc=7
```

Checkpoint 8 means the handler reached the reply; `gattStatus=0x0` is `BLE_GATT_STATUS_SUCCESS`;
`replyRc=7` is **`NRF_ERROR_INVALID_PARAM`**. The SoftDevice was rejecting the reply itself, so no
ATT response was ever transmitted and the client's write hung until something else dropped the
link.

A **write** authorize reply carrying `BLE_GATT_STATUS_SUCCESS` must also set `update = 1` and hand
`offset`, `len` and `p_data` back to the SoftDevice. Replying SUCCESS with `update = 0` and
`p_data = NULL` is invalid. Error statuses need none of this -- which is exactly why every rejection
path worked correctly and only the success path, the one that mattered, failed silently. Fixed in
`dfuReplyStatus()`.

### Client: after a buttonless jump the bootloader keeps the application's address

`ble_ota_update.py` (and `dfu_cli.py` before it) assumed the bootloader advertises at
`application address + 1`. Measured: after a `ResetSafeBLEDfu` jump the bootloader comes up on the
**same** address as the application -- the peer-data handoff at `0x20007F80` deliberately carries
the connection's address across the reset. `+1` applies only to a bootloader that starts with no
valid application, which is why it looked right after the SWD flash.

The client was scanning an address nothing was on while the bootloader sat there advertising, and
reported "the jump did not take effect" for a jump that had worked. It now waits on both addresses
and disambiguates with the DFU service UUID, since the address alone no longer identifies which
image is running.

### Also fixed, and correct regardless

The reset no longer happens inside the BLE event callback. `sd_ble_gatts_rw_authorize_reply()`
returning `NRF_SUCCESS` means the SoftDevice accepted the response for transmission, not that it
has been sent; resetting immediately can tear the radio down first. The callback now authorizes and
records a pending request, and the application loop calls `completeDfuReset()` after
`DFU_RESPONSE_SETTLE_MS` (150 ms).

### Method note

Three successive theories (prepared writes, Bleak version, Windows GATT cache) were all wrong and
all cost a full test cycle each. The checkpoint instrumentation answered it in one. On this board a
flash is ~30 s -- instrument early rather than reasoning further from source.

## 2026-09-03 late: two real defects in the buttonless-jump path, both now fixed

The re-ID of the bench board could not be delivered over BLE. Every jump attempt reported success
and nothing happened. Root-caused to two independent bugs in `ResetSafeBLEDfu.cpp`, plus one in
the third-party client.

### Client: nrf_dfu_py reports a jump it never performed

`dfu_lib.jump_to_bootloader()` wraps its control-point write in `except Exception: pass` and then
logs `"Jump command sent."` unconditionally. A rejected, timed-out, or dropped write is
indistinguishable from a real one. Do not trust that line. `scripts/ble_ota_update.py`
replaces it and treats "the bootloader actually advertised" as the only proof of a jump.

`dfu_cli.py` also lacks the Windows WinRT workaround that `ble_dfu_recover.py` carries (STA
apartment, message pump, RANDOM address binding). Measured on the bench: **28 s** from connect to
control-point write without it, **2.17 s** with it. `OTA_CONNECTED_HOLD_MS` is 30 s, so the old
path was losing a race it could not have been expected to win.

### Firmware bug 1: the authorize handler could return without replying

`resetSafeDfuWrite()` returned early -- with no `sd_ble_gatts_rw_authorize_reply()` -- whenever the
request was not a plain write to its value handle, notably for queued/long writes. An unanswered
`BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST` leaves the peer's write pending indefinitely; the only thing
that ended it was this application's own idle-peer disconnect at `OTA_CONNECTED_HOLD_MS`. From the
client it looked like a lost connection 28 s after a healthy connect.

Why a long write at all: `control_.setMaxLen(23)` equalled the default ATT MTU, and therefore
exceeded the 20 bytes a single write request can carry at that MTU. Windows answers that by using
the queued-write procedure even for the two-byte enter-bootloader opcode.

Fixed both ends -- every path out of the handler now replies exactly once, and `setMaxLen(20)`
keeps clients on the simple-write path. The characteristic only ever receives a two-byte opcode.

### Firmware bug 2: the OTA power gate could never reject anything

The `gDfuAllowed` test ran *after* a `BLE_GATT_STATUS_SUCCESS` reply had already been sent, so its
`BLE_GATT_STATUS_ATTERR_WRITE_NOT_PERMITTED` was a second reply to an already-answered request --
refused by the SoftDevice with `NRF_ERROR_INVALID_STATE` and never delivered. A probe below
`OTA_MIN_BATTERY_MV` would have told the client its write succeeded and then simply not rebooted.
Reordered to decide first and reply once.

### Ruled out along the way (tested, not argued)

Bleak version (1.1.1 and 3.0.2 fail identically), the Windows GATT handle cache
(`BluetoothCacheMode.UNCACHED`, control point resolves to handle 17, identical failure), the power
gate itself (advertisement flag bit4 set, 4323 mV), DFU service registration (all three
characteristics present, revision reads `0x0001` app-mode), and CCCD (`start_notify` completes in
30 ms). The wake counter in the advertisement was the decisive instrument throughout: it
incremented 19->20, 22->23, 27->28 and 29->30 across four jump attempts, proving the board never
reset and the write never took effect.

### Transfer speed: the 20-byte chunking was never a hardware limit

`dfu_lib._setup_mtu()` returns a hard-coded 23 whenever `high_mtu` is false, without ever asking
the link. `ble_dfu_recover.py` defaults it false, so **every transfer to date has been pinned to
20-byte chunks without measuring anything** -- about 6,550 ATT packets for a 131 KB image. The
bootloader's own `sdk_config.h` sets `BLEGATT_ATT_MTU_MAX = 247`, and Bleak's `mtu_size` is a
direct read of WinRT's `GattSession.max_pdu_size`. `ble_ota_update.py` enables high MTU by default,
which should give 244-byte chunks. **Not yet measured against the bootloader** -- do not quote a
speedup figure until a real transfer confirms it. Note the application link negotiates 23; only
the bootloader is configured for 247, and the transfer is what runs in the bootloader.

## `DEFAULT_TO_OTA_DFU` removes USB serial recovery

Worth knowing before assuming USB can bail you out:

```c
#ifdef DEFAULT_TO_OTA_DFU
  if (!valid_app || dfu_start) { _ota_dfu = 1; }
#endif
```

Whenever the application is invalid, OTA mode is forced — and this overrides `serial_only_dfu`
too. With no valid app, **USB serial DFU is unreachable**. BLE and SWD are the only ways in. This
is desirable once a probe is coated, but it is why USB could not deliver the application here.

## Options for resuming, in recommended order

1. **Recover over SWD first** (`DisplayCenter/arduino/Treebeard_ESP32_SWD_Flasher`,
   `scripts/swd_flash_hex.py`). Puts the board back to a known-good stock bootloader plus a
   working application, so debugging no longer happens on a board with no fallback.
2. **Rebuild the bootloader with `DUALBANK_FW=0`**, keeping `SIGNED_FW=1` and
   `DEFAULT_TO_OTA_DFU=1`, then re-migrate over USB while still recoverable. Single-bank is
   Adafruit's tested path. The property that actually prevents bricking — no valid app implies BLE
   recovery — is provided by `DEFAULT_TO_OTA_DFU`, not by dual-bank, and it is already proven.
   Dual-bank only avoids downtime during a failed update.
3. **Only if dual-bank is genuinely wanted**, debug the activation path. Each iteration is a
   ~12-minute BLE transfer and flash cannot be read without SWD, so this is slow and partly blind.

Do **not** attempt a bootloader update over BLE as the next step. A failed bootloader update is the
one failure BLE recovery cannot repair.

## Rebuilding the toolchain (all of it is gitignored)

Everything under `MSV3/.arduino-build/` must be recreated:

```powershell
# BLE DFU client, pinned to the combination proven on 2026-09-02
cd MSV3\.arduino-build
git clone https://github.com/recrof/nrf_dfu_py.git      # commit 409e4b7
python -m venv dfu-venv
.\dfu-venv\Scripts\python.exe -m pip install -r nrf_dfu_py\requirements.txt
.\dfu-venv\Scripts\python.exe -m pip install "bleak==1.1.1"   # pip resolves to 3.x, a major jump
```

`--wait` is **required** when targeting a running application: it advertises roughly 5 s per
minute and the client's default 5 s scan will miss it. Not needed against `AdaDFU`, which
advertises continuously.

To deliver an application to a board already sitting in the bootloader, do **not** use
`dfu_cli.py` — it always tries to find a running application and buttonless-jump it first, which
cannot work when there is no application. Drive `dfu_lib` directly and call `perform_update()`
against the `AdaDFU` device.

The bootloader source clone (`MSV3/.arduino-build/Adafruit_nRF52_Bootloader`, upstream tag
`0.11.0`) carries one local modification: `src/dfu_init.c` changes `ADAFRUIT_DEVICE_TYPE` from
`0x0052` to `0x3356`, plus an untracked `src/boards/msv3_e73/`. Reapply both after re-cloning.

## Documentation corrections still outstanding

- `MSV3/bootloader/README.md` describes device type `0x3356` as "MSV3-only ... rather than the
  generic Adafruit `0x0052`". The diff shows `0x3356` merely replaces Adafruit's upstream constant.
  It is a two-byte tag that stops stock-packaged images from installing; it is not model-specific
  and not a security property. Reword before it is mistaken for one.
- Nothing in this work has been validated against an interrupted transfer, which remains the real
  acceptance test (adoption sequence item 6 in `MSV3_BLE_OTA_PROOF_2026-09-02.md`).

## Do not conformal-coat yet

Unchanged and now better evidenced: an application cannot currently be installed on this
bootloader configuration at all. Coating a board in this state would leave BLE as the only access
path, on a configuration that has not yet successfully booted an application.
