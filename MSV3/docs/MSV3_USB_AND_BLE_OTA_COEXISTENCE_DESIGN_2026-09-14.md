# MSV3 USB + BLE OTA coexistence: design

**Date:** 2026-09-14

**Status:** Implemented as bootloader **1.0.4**. Builds, links, and passes both release map checks.
**Installed on nothing, and not bench-proven.** No probe runs it and none may until the acceptance
list in the requirement document passes on probe 99. A build is not evidence about behavior.

**Requirement this answers:** `MSV3_USB_AND_BLE_OTA_COEXISTENCE_REQUIREMENT_2026-09-14.md`. That
document owns the acceptance criteria; this one owns the mechanism. If the two disagree, it wins.

**Artifacts:**

- `MSV3/bootloader/patches/msv3-1.0.4-usb-ble-coexist.patch` (applies on top of 1.0.2 + 1.0.3)
- `MSV3/bootloader/generated/1.0.4/msv3-e73-recovery-signed-s140-6.1.1.hex`,
  sha256 `47cf4e9b0a1412261efc71b42eec9b0579471921fe75a53283b52f8aee11330c`
- registered in `build_msv3_release.ps1` with a source sentinel and two link-map checks

**Cost:** FLASH 32,888 B (84.52%) on 1.0.3 to 33,208 B (85.34%) on 1.0.4 -- **+320 bytes**, leaving
about 5.7 KB in the 38 KB bootloader region. RAM grew 8 bytes. That headroom is the binding
constraint on anything further added here.

## What it does

**USB is a normal transport, not a window.** Whenever the bootloader is in DFU mode and a cable is
present, USB CDC serial DFU is up -- for as long as the cable is there, with no timer to miss and no
mode to select. **BLE OTA runs on top of it**, at the same time, not instead of it. Whichever
transport a host actually uses is the one that performs the update.

With no cable, `VBUSDETECT` is clear, `usb_init()` is never called, and a battery/solar probe
behaves byte-for-byte as it does on 1.0.3. The proven invalid-application BLE escape is untouched.

## Why this is possible, and what actually blocked it

The requirement document describes USB and BLE as mutually exclusive in the pinned bootloader. As
built that is true, but it is a policy choice in a single if/else in `src/main.c`:

```c
if (_ota_dfu) { ... ble_stack_init(); } else { usb_init(serial_only_dfu); }
```

with `DEFAULT_TO_OTA_DFU` forcing `_ota_dfu` whenever `!valid_app || dfu_start` -- so the USB branch
is unreachable in exactly the case that matters. Three things in the pinned upstream show the stacks
themselves were never the obstacle:

- `usb_init()` already supports a running SoftDevice. It branches on `sd_softdevice_is_enabled()`,
  routes power events through `sd_power_usbdetected/usbpwrrdy/usbremoved`, and sets `USBD_IRQn` to
  priority 2 with 0-1 reserved for the SD.
- `wait_for_events()` already calls `tud_task()` whenever `tusb_inited()`, **regardless of DFU
  mode**, and `proc_soc()` already forwards `NRF_EVT_POWER_USB_*` into `tusb_hal_nrf_power_event()`.
- **Serial DFU *is* USB CDC.** The SLIP layer's `tud_cdc_rx_cb()` feeds the decoder directly from
  `tud_cdc_read_char()`. There is no UART in the path.

The real blocker was narrower and further down: `dfu_register_callback()` holds a **single**
`m_data_pkt_cb`, and single-bank DFU erases the application *before* Init authentication. Two
transports feeding one bank would destroy the image with no recoverable copy on either side.

## The arbiter

`dfu_transport_claim()` in `dfu_single_bank.c` resolves that, and it is the heart of the change:

- The first transport to present traffic **while the state machine is idle** owns the session, and
  its callback becomes the active one.
- A transport that already owns the session always succeeds (the common case mid-transfer).
- Any other transport is refused while `m_dfu_state != DFU_STATE_IDLE`.
- `dfu_init()` clears ownership, so each session arbitrates fresh.

Refusal is handled politely at each end. The serial path extracts and consumes the packet without
acting on it, so a wired host simply sees no acknowledgement and retries. The BLE path returns
`NRF_ERROR_INVALID_STATE` through `dfu_error_notify()`. In both cases the in-flight image is
untouched.

`bootloader_dfu_start_dual()` opens both transports in one session. `dfu_init()` is called once for
the pair, deliberately: it creates and starts the DFU inactivity timer, so calling it per transport
would re-create a live `app_timer`. Serial is started first so the SLIP decoder is armed before a
wired host's first frame can arrive; that ordering does not decide the winner, the arbiter does.
Teardown closes both, because the `is_ota()` global cannot name the owner in a dual session.

## Deliberately out of scope

- **No MSC/UF2 drag-and-drop.** `usb_init(true)` -- CDC only. UF2 writes unsigned images by
  definition and would route a host straight around signed-init verification, the device-type check
  and the monotonic version floor, which are the three things the fleet's update safety rests on.
- **No change to `DEFAULT_TO_OTA_DFU` semantics.** The invalid-application BLE escape is unchanged.
- **No change to signed-init, device-type, or version-floor enforcement.**

## Scope split (maintainer direction, 2026-09-14)

**The USB recovery path is MSV3-only.** It is remediation for one installed artifact: the resident
1.0.3 bootloader on E73-2G4M08S1C boards whose castellated SWD pads are covered by conformal coating
on exactly the units that most need a wired escape. MSV4 will decide its own recovery story against
its own hardware, possibly a connector that makes this moot, and nothing here should constrain that.

**The signed BLE OTA architecture is MSV3 and MSV4.** Device type `0x3356`, signed-init
verification, the monotonic version floor, boot-guard/bootloader pairing, the fleet registry and the
fail-closed preflight are meant to outlive this generation. The USB work must not add an MSV3
special case to any of them; if it ever requires weakening one, the design is wrong.

## Risks, in the order they are likely to bite

1. **Both stacks are now genuinely live at once.** This is the substantive change from 1.0.3 and the
   main thing the bench must exercise. `USBD_IRQn` at priority 2 leaving 0-1 to the SoftDevice is
   upstream's own arrangement, but MSV3 has never run both together. Watch a BLE DFU for dropped
   packets or PRN timeouts while USB is enumerated but idle.
2. **Current draw and brownout on a harvested board.** Enabling the USB peripheral has
   HFCLK/regulator consequences a solar/battery probe may not tolerate. The VBUS gate is the
   mitigation -- no cable means USB is never initialized -- but this must be measured, not assumed.
3. **Arbiter correctness under a real race.** Two hosts starting at once is the case that decides
   whether the bank is safe. It needs a deliberate test, not code review.
4. **First-frame loss across the handover.** Serial is armed before BLE precisely to avoid it, but
   whether a wired host's opening frame survives in every timing is unverified.
5. **Flash headroom.** 85.34% used. There is room for a fix, not for another feature.

## Bench plan

All of it on **probe 99**, the permanently USB- and SWD-recoverable board. No coated or deployed
probe participates until every acceptance criterion has passed there.

1. SWD-install 1.0.4 on probe 99 with the existing rules, including the 3-second contact gate --
   which is not optional because this is a bench board.
2. Run the requirement document's acceptance list. The two that most need negative controls: an
   **erased** application must still reach persistent BLE recovery with **no cable attached**, and a
   **rejected lower-version package** must leave at least one verified recovery path.
3. Prove both transports independently: a full wired update with BLE idle, and a full BLE update
   with a cable attached and enumerated.
4. Prove the arbiter: start on one transport, then attempt a Start on the other mid-transfer, and
   confirm the first completes and the second is refused rather than corrupting the bank.
5. Power-loss and interrupted-transfer matrix on both, proving which path appears after each
   failure and that tooling reports it rather than inferring it from COM-port enumeration.
6. Only then write a fleet migration procedure -- which must account for probes whose SWD pads are
   under coating, the population that motivated all of this.

## What would make this design wrong

- If both stacks live together destabilizes BLE DFU on real hardware, fall back to a sequential
  policy -- a bounded wired window when VBUS is present, then teardown and BLE. That was an earlier
  draft of this document and it is a safe retreat, at the cost of a delay before BLE appears.
- If `VBUSDETECT` proves unreliable on the MSV3 harvester rail -- solar input misreading as bus
  power -- the gate moves to an explicit host handshake.
- If any of it requires relaxing signed-init, device-type or version-floor enforcement, it does not
  ship. The wired path is a convenience; those three are why the fleet is safe to update at all.

## How 1.0.4 actually gets onto a board: does it need SWD?

Answered 2026-09-14 by reading the DFU stack, not by assumption. **Mechanically it does not require
SWD. Practically, for the already-coated probes, it does -- for now.**

A bootloader update over the air is genuinely supported. `dfu_start_pkt_handle()` dispatches
`IS_UPDATING_BL()` to `dfu_activate_bl()`, and that path is present in the single-bank build, not
only the banked one. It is not theoretical either: probe 4 took a SoftDevice + bootloader +
application package over the air from the stock loader on 2026-09-03 with `swd_accessible: false`.

Two things stand in the way today, and neither is a property of 1.0.4 itself.

**1. The package type does not exist.** `build_msv3_release.ps1` builds SD+BL+APP only with
`--dev-type 0x0052`, a deliberate one-time bridge for the generic stock loader. Once a probe is on
MSV3's own `0x3356`, the only artifact the script produces is application-only; the bootloader is
archived as a merged hex, which is an SWD/flash artifact. Delivering 1.0.4 over the air needs a
`--dev-type 0x3356 --bootloader` package, and it needs the fleet tooling taught about bootloader
packages: `msv3_fleet.py` binds a package to a probe by reading the sensor identity and application
version out of the image, and a bootloader package carries neither, so every wrong-target guard
downstream of that binding is currently undefined for it.

**2. There is one unrecoverable window.** Staging is safe: `dfu_prepare_func_app_erase` erases the
application first, and an interruption there leaves an invalid application, which is exactly the
condition `DEFAULT_TO_OTA_DFU` turns into persistent BLE recovery. The swap is not safe. Losing
power while the bootloader itself is being rewritten leaves it half-written with no SWD underneath
it on a coated board, and nothing recovers that.

So the deployment picture:

| Board | Path |
|---|---|
| Probe 99 (bench) | SWD. No risk, and where the acceptance list runs. |
| Probe 7 (`swd_accessible: true`) | SWD. Straightforward. |
| Probes 4, 5, 6, 8 (coated) | OTA possible in principle. Needs the package type built, proven on probe 99, and a deliberate decision about the swap window. |
| Every board provisioned from now on | SWD at provisioning, **before** coating. Free. |

**Treat 1.0.4 as forward-looking rather than a retrofit.** There is an irony worth stating plainly
so nobody re-derives it later: this release exists to give conformally coated probes a wired escape,
and the coated probes are precisely the population for which delivering it carries the most risk.
Its real value is on every board built from here, provisioned with 1.0.4 before the coating goes on.
Retrofitting the existing coated four is a separate decision with its own evidence requirement, and
should not be folded into "install the new bootloader".

## Verification note worth keeping

The link-map check caught a real failure on the first 1.0.4 build: the USB helper did not appear in
the map at all. It had been inlined, not excluded -- `check_dfu_mode()`, which certainly exists, is
likewise absent because it is inlined into `main()`. The helper is now `noinline` so the artifact
can be proven, and the release build checks for both `msv3_usb_attach_if_vbus` and
`dfu_transport_claim`.

The general point is worth more than the instance: a source sentinel proves a patch *applied*, not
that the code reached the binary. For a bootloader that cannot be re-flashed on a coated probe,
those are different questions and both need answering.
