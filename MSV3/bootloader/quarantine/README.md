# Quarantined signed application packages

**Do not install anything in this directory on any probe.** Nothing here is deleted, and nothing
here is broken. These are validly signed packages that must simply never be reached for by
accident.

Quarantined 2026-09-06, while preparing probes 4 and 5 for conformal coating.

## Why these two

Both are **pre-guard-1** application images. Guard 1 is the change that stops a sensor fault from
closing the radio:

```c
// before -- a failed measurement suppressed the advertisement entirely
bool shouldAdvertise = measurementOk && (intervalElapsed || bigSwing);
// after -- advertise on the interval regardless, and mark the reading instead
bool shouldAdvertise = intervalElapsed || (measurementOk && bigSwing);
```

Installing one of these on a coated probe would re-introduce the defect where a dead sensor, a
stuck I2C bus or water in the connector silently closes the only channel through which that probe
can ever be reached again. **A rollback here is not a lost feature, it is a lost probe.**

They are also both built at sensor identity `MSV3-MOISTURE-005`, so installing either on probe 4
would put two probes on the same sensor id and mix two plants' data in the receiver — which has
already happened once on this project.

## Why quarantine rather than delete

They are the only surviving artifacts of the 1.0.0 and 1.0.1 releases, and their hashes appear in
`../generated/1.0.0/release-manifest.json` and `../generated/1.0.1/release-manifest.json`. Deleting
them would destroy the ability to reproduce or audit what was actually shipped. `SHA256SUMS.txt`
here records both; the 1.0.1 hash was re-verified against its manifest at quarantine time and
matches.

Their absence from `../generated/<version>/` means those two manifests no longer describe a
complete directory. That is deliberate and is recorded here rather than by editing the manifests,
which are as-built records and should not be rewritten.

## What replaced them

`../generated/1.0.2/` carries per-probe, per-version packages whose names state both:

```
msv3-1.0.2-sensor004-v65550-signed-ota.zip
msv3-1.0.2-sensor099-v65549-signed-ota.zip
```

## What actually stops these being installed

Three independent things, listed weakest first:

1. **Being in this directory.** Weakest, and the reason the other two matter.
2. **The host preflight's identity check.** `scripts/msv3_fleet.py` extracts the
   `MSV3-MOISTURE-NNN` string from the image and refuses a package whose identity disagrees with
   the registry entry for the target probe. Both packages say `005`, so both are refused for
   probes 4 and 99 today.
3. **Bootloader 1.0.2's anti-rollback**, once installed. Version 65537 and below now sits under
   the floor these probes will carry, and the device itself refuses it — the first of these three
   that does not depend on anyone running the right tool.

Until 1.0.2 is installed, only (2) is real: **bootloader 1.0.1 accepts any validly signed image at
any version**, measured on probe 99 on 2026-09-05.
