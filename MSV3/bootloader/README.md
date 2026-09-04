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

The application version is explicit in every package. Bootloader 0.11.0 does
not persist application versions, so it cannot enforce anti-rollback by itself;
the release manifest and package naming prevent accidental rollback, while the
private signing key prevents arbitrary third-party images. Do not describe
cryptographic anti-rollback as implemented.

Single-bank DFU erases the running application before writing its replacement, so
an interrupted update does not preserve the old application. It does remain
recoverable: an erased or invalid application makes the bootloader advertise its
BLE DFU service indefinitely. Release 1.0.0's experimental dual-bank image must not
be installed; on the bench it accepted and validated the application transfer but
failed the subsequent bank-0 boot validation.
