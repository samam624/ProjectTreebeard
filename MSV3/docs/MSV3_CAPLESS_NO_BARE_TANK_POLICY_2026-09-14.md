# MSV3 Capless / No-Bare-Tank Policy (2026-09-14)

## Decision

All healthy MSV3 moisture probes move to a capless sensing tank: C17 is removed (DNP on future
assemblies), the FDC2214 measures only electrode-connected TMUX1109 codes 0 and 1, and production
firmware never selects codes 2 or 3. The legacy 23-byte BLE packet is retained, but the bare field
is zero, its validity bit is clear, and flag bit 6 identifies the capless hardware/scale.

This is a system invariant, not a runtime option. A capless probe must not run an old production
image that still attempts a bare conversion.

## Probe 8 evidence

All measurements used the same 6.8uH L2 and IDRIVE code 16 (0.169mA):

| State | C17 installed | C17 removed |
|---|---:|---:|
| Connected phase 0 | 20,493,434 | approximately 39,406,000 |
| Connected phase 1 | 20,493,867 | 39,409,418 mean |
| Connected frequency | 3.380MHz | 6.499MHz |
| Inferred connected capacitance | approximately 326pF | approximately 88pF |
| Two-hand response | approximately 3M counts historically | approximately 12M counts (39M to 27M) |

Both capless connected phases were stable and reported STATUS=0x8 with no amplitude warning. The
phase difference was approximately 0.008%.

A deliberate mux-2 test at minimum IDRIVE code 0 returned DATA_CH0=268,435,455 (0x0FFFFFFF, the
exact FDC2214 over-range sentinel) and STATUS=0x808/ERR_WD. The measured capacitance change implies
roughly 25pF for the capacitor-free bare tank, consistent with the approximately 30pF TMUX/board
estimate and an 11-12MHz resonance. That exceeds both the configured FIN_SEL=01 differential range
(0.01-8.75MHz) and the FDC2214 sensor-frequency ceiling (10MHz). Increasing IDRIVE cannot correct
frequency over-range and only increases amplitude stress.

The real-pot study found that bare-reference normalization removed only 11.7% of the final day's
daily harmonic. The dominant daily error remained in bulk soil temperature and the probe/soil
interface, neither of which the disconnected electronics-adjacent tank observes. Giving up that
small correction is justified by the approximately fourfold measured raw-count response.

## Firmware and receiver invariants

- Production FDC IDRIVE defaults to code 16.
- Production mux selections are only code 0 or 1, made while the FDC is shut down.
- No advertising wake performs a second FDC wake/conversion.
- BLE flag bit 3 (bare valid) is always clear and bytes 19-22 are zero.
- BLE flag bit 6 is always set by capless production firmware.
- The receiver treats bit 6 as MSV3 moisture scale 3, accepts the higher connected-count range,
  uses the capless presentation mapping, and starts a new cloud calibration epoch.
- Bare normalization and the learned temperature-compensation channel are off for scale 3. Raw
  connected counts and board temperature remain available for later soil/interface modeling.
- The large-swing advertising threshold is 1,200,000 counts, four times the former threshold, so
  the measured gain does not create extra radio wakes for the same physical disturbance.

## Rollout order

1. Build and install the receiver firmware that understands flag bit 6 and scale 3. During bench
   rework, keep receivers from ingesting transitional readings if practical.
2. For each healthy probe, install a signed capless production image with a monotonically higher
   application version while C17 and stable power are still present.
3. Fully remove USB, battery, and solar power. Remove C17; do not merely leave a loose lifted end on
   a field unit. Restore coating over the rework site where applicable.
4. Power up and verify both connected phases, no ERR_WD/ERR_AHW/ERR_ALW, frequency below 8.75MHz,
   and tank amplitude across the dry-to-wet range. Recalibrate on scale 3.
5. Do not run mux 2/3 as a functional test. A scope-only engineering experiment requires an
   explicit diagnostic image and minimum drive; its FDC data is out of range by definition.

Probe 7 was initially excluded because of a phase-dependent TMUX fault. Bench work later on
2026-09-14 proved that fault was a pressure-sensitive code-1 solder connection and repaired it by
reflow. Both connected phases now overlap near 36.73M with no FDC error bits. Probe 7 can therefore
join the capless rollout after its archived v65565 production restore replaces the currently
installed v65565 bring-up diagnostic.

## Prepared fleet packages (not installed)

Application version 65564 is reserved for the original capless fleet transition; Probe 7 uses
v65565 because its diagnostic had to advance beyond its previously installed rollback floor. Every
package below was compiled with IDRIVE 16, checked for its embedded sensor
identity/version/signature, and added to a release directory that passes
`scripts/verify_msv3_release_archive.py`.

| Probe | Installed bootloader contract | Signed OTA package | SHA-256 |
|---:|---|---|---|
| 4 | 1.0.1, Guard 2 off | `msv3-1.0.1-sensor004-v65564-signed-ota.zip` | `466b6a11ae1995963e7c8058de5aec1fc67dedcaea23a91ebdbfd983e809853c` |
| 5 | 1.0.3, Guard 2 on | `msv3-1.0.3-sensor005-v65564-signed-ota.zip` | `9c31e04e058454bdd9ada5ad297a14934b9e216f62f77b1bceb06679fe77ab81` |
| 6 | 1.0.3, Guard 2 on | `msv3-1.0.3-sensor006-v65564-signed-ota.zip` | `daf2c13aeda69c2687c9eead3815182af3122837ce67402f5483bdb8fc802893` |
| 7 | 1.0.3, Guard 2 on | `msv3-1.0.3-sensor007-v65565-signed-ota.zip` | `2ed999de3cb189f2f353fb562de615b2b9db5afc29e766ee086d423f69f5b456` |
| 8 | 1.0.3, Guard 2 on | `msv3-1.0.3-sensor008-v65564-signed-ota.zip` | `ed2ef2ddd35576038ce1ca1144f7a361ad1f4ece23a2ee1cfebfe6b0d6f7e9dc` |

Do not install these packages until a capless-aware DisplayCenter receiver is running. The probe
registry must continue to report the actually installed application version until each OTA
transfer and DIS/build-ID readback succeeds; package preparation alone is not deployment.
