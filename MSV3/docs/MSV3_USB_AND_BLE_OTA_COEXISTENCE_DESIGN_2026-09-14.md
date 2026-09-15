# MSV3 USB + BLE OTA coexistence: design

**Date:** 2026-09-14

**Status:** Design started. Nothing here is implemented, built, or bench-proven. No fleet probe
may be migrated on the strength of this document.

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

## Proposed design: VBUS-gated dual transport

Bring up **BLE unconditionally** and **USB additionally when bus power is actually present**.

1. In the DFU entry block, always take the BLE path: `mbr_init_sd()`, `ble_stack_init()`. This
   preserves today's proven behavior as the floor -- an erased or invalid application still lands in
   indefinite BLE recovery with no host attached and no user action.
2. Then read `sd_power_usbregstatus_get()`. If `VBUSDETECT` is clear, stop -- BLE-only, exactly as
   now, and no USB cost on a battery/solar probe.
3. If VBUS is present, additionally call `usb_init(true)` (CDC only; see below on MSC) so the same
   bootloader session serves a wired host.
4. Run one event loop that services both: the existing `proc_soc()` already dispatches SD events and
   forwards USB power events, so the loop shape does not need to change as much as the transport
   count suggests.
5. Whichever transport receives a valid DFU Start first wins the session; the other is torn down
   before any flash write begins, so two hosts can never interleave writes into one image.

Point 5 is a requirement, not an optimization. Single-bank DFU erases the old application before
Init authentication, so two concurrent transfer state machines pointed at one bank is a way to
build a brick.

### Deliberately not in scope for the first version

- **No MSC/UF2 drag-and-drop.** `usb_init(cdc_only = true)`. UF2 writes unsigned images by
  definition; adding a filesystem that accepts arbitrary firmware to a fleet whose entire safety
  story is signed, version-floored packages would undo the OTA work. Wired update goes through the
  same signed serial DFU protocol or it does not ship.
- **No change to `DEFAULT_TO_OTA_DFU` semantics.** The invalid-application BLE escape stays exactly
  as it is. This design only adds a transport when VBUS says a host could plausibly be there.
- **No change to the signed-init, device-type, or version-floor checks.** See the scope section.

## Risks, in the order they are likely to bite

1. **Current draw and brownout on a solar/battery probe.** Enabling the USB peripheral needs
   HFCLK/regulator behavior that a harvested board may not tolerate. The VBUS gate is the mitigation:
   with no bus power, USB is never initialized at all, so a field probe's behavior is byte-identical
   to today. This must still be measured, not assumed.
2. **Interrupt latency against the SoftDevice's radio timing.** `USBD_IRQn` at priority 2 is
   upstream's own choice and leaves 0-1 to the SD, but MSV3 has never actually run both at once.
   A BLE DFU that now shares the core with USB servicing is the case to watch for dropped packets
   or PRN timeouts.
3. **A host seeing a CDC port and assuming the bootloader owns it.** This is the failure the
   requirement document called out and it is a *tooling* problem as much as a firmware one. The
   updater must report which transport it is actually talking to rather than inferring it from COM
   port enumeration.
4. **Flash-write contention.** Addressed by the single-winner rule above; it needs a real test with
   both transports connected, not just code review.

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
