# MSV3 recoverable OTA bootloader

`scripts/build_msv3_release.ps1` builds a board-specific Adafruit nRF52
bootloader from the pinned upstream 0.11.0 source and creates the production
application packages. The release configuration enables:

- dual-bank application updates, so an interrupted transfer keeps the old app;
- BLE recovery when no valid application exists (`DEFAULT_TO_OTA_DFU`);
- NIST P-256 signed DFU packages, with UF2 deliberately disabled;
- MSV3-only device type `0x3356`, rather than the generic Adafruit `0x0052`;
- the real active-high status LED on P1.10 and no nonexistent DFU buttons;
- SoftDevice S140 6.1.1 and its required internal-RC LFCLK boot path.

The private key defaults to `MSV3/.secrets/msv3-dfu-signing-private.pem`, which
is ignored by Git. The generated bootloader contains its public key, so losing
the private key after the SWD pads are coated would make future signed updates
impossible. Make an offline backup before coating a production board.

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
