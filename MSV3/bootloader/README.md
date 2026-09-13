# MSV3 recoverable OTA bootloader

`scripts/build_msv3_release.ps1` builds a board-specific Adafruit nRF52
bootloader from the pinned upstream 0.11.0 source and creates the production
application packages. The release configuration enables:

- single-bank application updates, using the Adafruit bootloader's default and bench-proven path;
- BLE recovery when no valid application exists (`DEFAULT_TO_OTA_DFU`);
- NIST P-256 signed DFU packages, with UF2 deliberately disabled;
- DFU compatibility tag `0x3356`, so generic Adafruit `0x0052` packages are rejected;
- the real active-high status LED on P1.10 and no nonexistent DFU buttons;
- SoftDevice S140 6.1.1 and its required internal-RC LFCLK boot path.

MSV3 release 1.0.2 added an on-device monotonic application-version floor after a signed BLE
activation. Release 1.0.3 retains that protection and reserves the final eight bytes of RAM at
`0x2003FFF8` for the application's watchdog-loop recovery counter. The application linker reserves
the same address. Guard 2 must not be enabled with bootloader 1.0.2 or older.

That rule is now ENFORCED rather than merely written down, in two places. Every image
stamps `MSV3-GUARD2:1` or `MSV3-GUARD2:0` into itself, and `scripts/msv3_fleet.py` reads it
back out of a package: `ble_ota_update.py` refuses to transfer a guard-enabled image to a
probe whose `bootloader` in `MSV3/fleet.json` predates 1.0.3, and
`MSV3/scripts/arduino_msv3_probe.ps1` refuses to build one. Measured 2026-09-09 from
`vector[0]` of each release hex: 1.0.1 and 1.0.2 boot with MSP `0x20040000`, so the retained
record is the first memory their stacks descend into; 1.0.3 boots with MSP `0x2003FFF8`.
An image built before the marker existed carries none, and is accepted only on a bootloader
that already reserves the record, or when its image SHA-256 is pinned in that probe's
`legacy_package_sha256`.

The private key defaults to `MSV3/.secrets/msv3-dfu-signing-private.pem`, which
is ignored by Git. The generated bootloader contains its public key, so losing
the private key after the SWD pads are coated would make future signed updates
impossible. The build now fails if the key is missing instead of silently making
an incompatible one. Use `-GenerateSigningKey` only when deliberately establishing
a new trust root that will be installed over SWD, and make an offline backup before
coating a production board.

Generated release artifacts and a SHA-256 manifest go under
`MSV3/bootloader/generated/<version>/`. The future OTA package is signed. The
separately named `initial-install-from-stock` package is deliberately unsigned
because the existing stock bootloader cannot parse Adafruit's signed extended
init packet; use it exactly once to migrate an accessible board, then retain it
only as a factory/SWD-adjacent artifact.

The application version is explicit in every package. Stock Adafruit 0.11.0 and MSV3 1.0.1 do not
persist it. MSV3 1.0.2+ stores a monotonic floor in the bootloader settings page and refuses a
lower signed application after authenticating its init packet. Equality is deliberately allowed
so the exact known-good rescue package remains reinstallable. `MSV3/fleet.json` keeps a second,
host-side version floor and must be updated after every successful transfer.

Single-bank DFU erases the running application before writing its replacement, so
an interrupted update does not preserve the old application. It does remain
recoverable: an erased or invalid application makes the bootloader advertise its
BLE DFU service indefinitely. Release 1.0.0's experimental dual-bank image must not
be installed; on the bench it accepted and validated the application transfer but
failed the subsequent bank-0 boot validation.

Do not update this bootloader over BLE. Install it once over SWD before coating, finish
provisioning with a signed BLE application install so the anti-rollback floor is seeded, verify the
running DIS build ID and advancing wake counter, archive the equality-version rescue package, and
back up the signing key offline. The current production checklist is
`MSV3/docs/MSV3_FINAL_OTA_PRECOATING_HANDOFF_2026-09-08.md`.
