# MSV3 recoverable-bootloader handoff — 2026-09-03 (IN PROGRESS)

Work stopped mid-bringup. The bench board is in a **safe but non-running state** and this document
is what is needed to resume cold.

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
- **Signature / device type / SoftDevice requirement.** All accepted, as above.
- **Dropped packets.** No `PRN Timeout` warnings were logged during the transfer.

**Prime suspect: `DUALBANK_FW=1`.** Adafruit ships single-bank by default; this build enables a
code path that is not their tested configuration. Earlier the same day, the same client and the
same PRN/chunk settings performed a flawless OTA against the *stock single-bank* bootloader, which
narrows the difference to the signed/dual-bank build.

Unexplained secondary observation: the transfer took **11 m 50 s** versus **3 m 31 s** for a
same-size image against the stock bootloader, with no PRN timeouts. Probably a slower negotiated
connection interval in the bootloader, but not confirmed.

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
   `MSV3/scripts/swd_flash_hex.py`). Puts the board back to a known-good stock bootloader plus a
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
