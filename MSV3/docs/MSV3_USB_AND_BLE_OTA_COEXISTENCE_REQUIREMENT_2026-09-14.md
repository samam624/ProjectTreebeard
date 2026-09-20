# MSV3 USB and BLE OTA coexistence requirement

**Date:** 2026-09-14

**Status:** Open requirement; not implemented or bench-proven. **Design started 2026-09-14** --
see `MSV3_USB_AND_BLE_OTA_COEXISTENCE_DESIGN_2026-09-14.md` for the proposed mechanism. This
document remains the owner of the acceptance criteria below; if the two disagree, this one wins.

**Scope decision, 2026-09-14 (maintainer):** the **USB recovery path is MSV3-only** -- remediation
for the resident 1.0.3 bootloader on this generation's hardware, not a platform feature, and it must
not constrain MSV4's own recovery design. The **signed BLE OTA architecture carries forward to MSV4
as well**: device type, signed-init verification, the monotonic version floor, boot-guard pairing,
the fleet registry, and the fail-closed preflight are the long-lived envelope. The USB work
therefore may not add an MSV3 special case to any of them.

## Required outcome

MSV3 must retain both of these maintenance paths on the same fleet image:

1. a dependable wired USB path for serial diagnostics and firmware replacement; and
2. signed BLE OTA, including automatic BLE recovery when the application is missing or invalid.

USB enumeration by a valid application must not be mistaken for proof that the bootloader can
accept a wired firmware image. Conversely, enabling automatic BLE recovery must not silently
remove the wired recovery path.

## Current limitation

The installed 1.0.3 bootloader is based on Adafruit nRF52 Bootloader 0.11.0 and is built with
`DEFAULT_TO_OTA_DFU`. That setting is valuable because an erased or invalid single-bank application
falls back to indefinite BLE DFU advertising. In the pinned upstream implementation, however, the
same compile-time branch bypasses `usb_init()`, so bootloader USB CDC/serial DFU is absent.

This matters because the application can still enumerate a COM port. A host can therefore see USB
and begin a USB-triggered workflow, only for the port to vanish when control reaches the BLE-only
bootloader. The 2026-09-14 Probe 7 incident also reinforced that single-bank Start erases the old
application before Init authentication; a rejected rollback package can therefore leave BLE
recovery as the only remaining path.

## Acceptance criteria

A replacement bootloader or maintenance workflow is not accepted until one physical MSV3 board
proves all of the following:

- A valid production application exposes its intended USB CDC diagnostics.
- A wired firmware update can be deliberately initiated and completed without SWD.
- The normal application-side BLE DFU handoff still works.
- An erased, corrupt, or absent application enters persistent BLE recovery automatically.
- Signed-init verification, device type `0x3356`, and the on-device monotonic version floor remain
  enforced on BLE updates.
- Equality-version rescue remains possible with the archived, board-matched package.
- A rejected lower-version package does not strand the device without at least one verified USB or
  BLE recovery path.
- Reset, power loss, and interrupted-transfer tests demonstrate which path appears after failure;
  tooling reports that state accurately instead of inferring it from COM-port enumeration.

## Constraints and next step

Do not disable `DEFAULT_TO_OTA_DFU` on deployed probes merely to regain USB; doing so would remove
the already-proven invalid-application BLE escape. First inspect the pinned bootloader's transport
initialization and determine whether USB and BLE transports can safely initialize together, or
whether a deliberate USB-presence/timeout policy is needed. Build this as a new bootloader version,
test it on an SWD-accessible bench board, and only then define a fleet migration procedure.
