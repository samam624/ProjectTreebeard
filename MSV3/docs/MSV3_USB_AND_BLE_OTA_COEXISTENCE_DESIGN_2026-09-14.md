# MSV3 USB + BLE OTA coexistence: design

**Date:** 2026-09-14

**Status:** Implemented and building as bootloader **1.0.4**. **NOT installed on any hardware and
not bench-proven.** No probe runs it, and none may until the acceptance list below passes on probe
99. A build is not evidence about behavior.

**Artifacts as of 2026-09-14:**

- `MSV3/bootloader/patches/msv3-1.0.4-usb-ble-coexist.patch`
- `MSV3/bootloader/generated/1.0.4/msv3-e73-recovery-signed-s140-6.1.1.hex`,
  sha256 `1436177299cb8ad43f3d79d2f0af2985e4d54bb74cd01d7d1cc0620b9a4edb8d`
- registered in `build_msv3_release.ps1` with a source sentinel and a link-map check

Cost: `.text` grew from 32,976 bytes (1.0.3) to 33,040. The bootloader sits at roughly 84.8% of
its 38 KB FLASH region, and that headroom is the binding constraint on anything further added here.

**Verification note worth keeping.** The link-map check caught a real failure on the very first
build: the helper did not appear in the map at all. It had been inlined, not
excluded -- `check_dfu_mode()`, which certainly exists, is likewise absent because it is inlined
into `main()`. The helper is now `noinline` so the linked artifact can be proven. The general point:
a source-level sentinel proves a patch applied, not that the code reached the binary, and for a
bootloader that cannot be re-flashed on a coated probe those are different questions.

**Requirement this answers:** `MSV3_USB_AND_BLE_OTA_COEXISTENCE_REQUIREMENT_2026-09-14.md`. That
document owns the acceptance criteria; this one owns the mechanism. If the two ever disagree, the
requirement wins.

## Scope, and why it is split

This is deliberately two pieces of work with two different lifetimes, and conflating them is the
main way this effort could damage something that already works.

**The USB recovery path is MSV3-only.** It is remediation for one specific installed artifact: the
resident 1.0.3 bootloader, derived from Adafruit nRF52 Bootloader 0.11.0, on E73-2G4M08S1C boards
whose castellated SWD pads are covered by conformal coating on exactly the units that most need a
wired escape. It is scoped to that generation and should not be treated as a platform feature.
MSV4 will have its own recovery story decided against its own hardware -- possibly a connector that
makes this whole problem moot -- and nothing here should constrain that choice.

**The signed BLE OTA architecture is MSV3 and MSV4.** Device type `0x3356`, signed-init
verification, the on-device monotonic version floor, the boot-guard/bootloader pairing, the fleet
registry, and the fail-closed preflight are the parts intended to outlive this generation. They are
the security and safety envelope, not a stopgap.

The practical consequence for whoever implements this: **the USB work must not reach into the OTA
layer.** No MSV3-specific special case may be added to init validation, version-floor enforcement,
device-type checking, or package identity. If a proposed USB change requires weakening any of
those, the design is wrong and should come back here rather than proceed.

## The finding that makes this tractable

The requirement document describes USB and BLE as mutually exclusive in the pinned bootloader. That
is true as built, but it is a **policy choice in one if/else, not a hardware or SoftDevice
limitation** -- and the pinned upstream already contains the plumbing for both to run together.

The mutual exclusion is here, in `src/main.c`:

```c
if (dfu_start || !valid_app) {
    if (_ota_dfu) {
      ...
      ble_stack_init();
    } else {
      led_state(STATE_USB_UNMOUNTED);
      usb_init(serial_only_dfu);
    }
```

and `DEFAULT_TO_OTA_DFU` forces `_ota_dfu = 1` for `!valid_app || dfu_start`, which is precisely the
set of cases where a wired path is wanted. So the `else` branch is unreachable exactly when it
matters.

But `usb_init()` in `src/usb/usb.c` **already handles a running SoftDevice**:

```c
  NVIC_SetPriority(USBD_IRQn, 2);   // "0, 1 is reserved for SD"
  ...
  if (is_sd_existed()) sd_softdevice_is_enabled(&sd_en);
  if (sd_en) {
    sd_power_usbdetected_enable(true);
    sd_power_usbpwrrdy_enable(true);
    sd_power_usbremoved_enable(true);
    sd_power_usbregstatus_get(&usb_reg);
  } else {
    nrfx_power_init(...);           // direct register path, SD absent
  }
```

and `proc_soc()` in `src/main.c` already forwards SoftDevice power events into TinyUSB:

```c
    int32_t usbevt = (soc_evt == NRF_EVT_POWER_USB_DETECTED) ? NRFX_POWER_USB_EVT_DETECTED :
                     (soc_evt == NRF_EVT_POWER_USB_POWER_READY) ? NRFX_POWER_USB_EVT_READY :
                     (soc_evt == NRF_EVT_POWER_USB_REMOVED) ? NRFX_POWER_USB_EVT_REMOVED : -1;
    if (usbevt >= 0) tusb_hal_nrf_power_event((uint32_t)usbevt);
```

Upstream wrote the SoftDevice-aware USB path, reserved the interrupt priorities for it, and routes
the events -- it simply never calls `ble_stack_init()` and `usb_init()` in the same boot. That is
the whole gap.

This does not make the change free. It means the risk is concentrated in scheduling and power
behavior rather than in "can these two stacks coexist at all", and the bench plan below is aimed at
that.

## Design: sequential wired-first window, not concurrent transports

**This reverses the first draft, which proposed running both transports at once.** That draft was
wrong, and the reason is worth keeping because it is not visible from `main.c` alone.

Concurrency looked right: `usb_init()` already supports a running SoftDevice, and `proc_soc()`
already forwards SoftDevice USB power events into TinyUSB (both quoted above). But bringing both
stacks up does not give you two DFU transports, and shipping it would have been **worse than doing
nothing**:

- `bootloader_dfu_start(ota)` starts exactly one transport -- `dfu_transport_ble_update_start()`
  **or** `dfu_transport_serial_update_start()`. Initializing USB without starting the serial
  transport enumerates a CDC port that accepts no DFU traffic: precisely the "a host can see USB and
  begin a USB-triggered workflow" failure the requirement document exists to prevent.
- `dfu_register_callback()` stores a **single** `m_data_pkt_cb`. A second transport's registration
  overwrites the first, so two transports cannot both be served.
- `bootloader_dfu_update_process()` dispatches its transport close on `is_ota()`, one global.

Serving both concurrently therefore means restructuring the shared DFU state machine -- the same
area whose restructuring was deliberately dropped from 1.0.2 scope after the completion-gate defect.
That is not a trade worth making on a bootloader that cannot be re-flashed on a coated probe.

### What 1.0.4 actually does

A sequential, wired-first, strictly bounded window, taken only on recovery entry:

1. **Only when there is no valid application.** An application that jumped here for a BLE OTA
   (`DFU_MAGIC_OTA_APPJUM`, `_sd_inited` already true) has a host waiting on the radio and must not
   be delayed. This narrows the change to the case that motivated it: a dead application on a board
   whose SWD pads are under coating.
2. **Only when `VBUSDETECT` is set.** With no cable the USB peripheral is never initialized, so a
   battery/solar probe behaves byte-for-byte as it does today. The SoftDevice is not yet enabled on
   this path, so `USBREGSTATUS` is read directly.
3. **CDC only, never UF2.** Mass storage writes unsigned images by definition and would route around
   signed-init verification, the device-type check and the monotonic version floor.
4. **Bounded by `MSV3_USB_DFU_WINDOW_MS` (10 s) with `cancel_timeout_on_usb = false`,** so a host
   that enumerates and then does nothing cannot hold the probe out of BLE recovery. This does not
   risk cutting off a real transfer: `bootloader_timeout_startup_dfu()` only exits when
   `m_startup_dfu_has_activity` is still false, so an update in progress makes the timeout a no-op.
   That activity guard is upstream behavior, not something this patch adds.
5. **Then tear USB down and fall through to the unchanged BLE path**, which remains the floor.

If step 1 or 2 declines, nothing is initialized and the whole branch is byte-for-byte 1.0.3.

The 10 s window is a starting value, not a measured one. It trades how long a dead-application probe
waits before BLE recovery appears against how long a human has to start a wired update. Bench work
should set it deliberately.

### Still deliberately out of scope

- **No MSC/UF2 drag-and-drop**, for the reason in point 3.
- **No change to `DEFAULT_TO_OTA_DFU` semantics.** The invalid-application BLE escape is untouched.
- **No change to the signed-init, device-type, or version-floor checks.** See the scope section.
- **No concurrent transports**, per the finding above.

## Risks, in the order they are likely to bite

1. **Current draw and brownout on a solar/battery probe.** Enabling the USB peripheral needs
   HFCLK/regulator behavior that a harvested board may not tolerate. The VBUS gate is the mitigation:
   with no bus power, USB is never initialized at all, so a field probe's behavior is byte-identical
   to today. This must still be measured, not assumed.
2. **Interrupt latency against the SoftDevice's radio timing.** Largely retired by going
   sequential: USB is torn down before `ble_stack_init()`, so the two stacks are never live
   together. What remains is whether `usb_teardown()` leaves the peripheral in a state that a
   following SoftDevice enable tolerates.
3. **A host seeing a CDC port and assuming the bootloader owns it.** This is the failure the
   requirement document called out and it is a *tooling* problem as much as a firmware one. The
   updater must report which transport it is actually talking to rather than inferring it from COM
   port enumeration.
4. **Flash-write contention.** Retired by going sequential -- only one transport is ever started,
   so there is no second state machine to interleave writes into the bank.
5. **A dead-application probe now waits up to 10 s before BLE recovery appears**, where 1.0.3
   advertised immediately. This is the real cost of the change and the reason the window is gated on
   VBUS: a probe on battery in the field never pays it.

## Bench plan

All of this happens on **probe 99**, the permanently USB- and SWD-recoverable board that every
image is proven on. No coated probe and no deployed probe participates until every acceptance
criterion in the requirement document has passed on probe 99.

1. Build the candidate as a new bootloader version (**1.0.4**), never by patching a board in place.
   It goes through `build_msv3_release.ps1` with the same sentinel verification the 1.0.2
   anti-rollback and 1.0.3 boot-guard patches use.
2. SWD-install on probe 99 and blank-check/read-back verify, per the existing SWD rules -- including
   the 3-second contact gate, which is not optional just because this is a bench board.
3. Run the requirement document's acceptance list in order. The two that most need a negative
   control: an **erased** application must still reach persistent BLE recovery with no USB attached,
   and a **rejected lower-version package** must leave at least one verified recovery path.
4. Power-loss and interrupted-transfer matrix on both transports, proving which path appears after
   each failure.
5. Only then write a fleet migration procedure -- and that procedure has to account for probes whose
   SWD pads are under conformal coating, which is the population that motivated the work.

## What would make this design wrong

Recorded now, so it is testable rather than arguable later:

- If measurement shows `usb_init()` with the SoftDevice enabled destabilizes BLE DFU on real
  hardware, the fallback is an explicit sequential policy -- USB for a bounded window when VBUS is
  present, then teardown and BLE -- rather than forcing concurrency.
- If the VBUS gate turns out to be unreliable on the MSV3 harvester rail (solar input misreading as
  bus power), the gate moves to an explicit host handshake instead.
- If any part of this cannot be done without relaxing signed-init, device-type, or version-floor
  enforcement, it does not ship. The wired path is a convenience; those three are the reason the
  fleet is safe to update at all.
