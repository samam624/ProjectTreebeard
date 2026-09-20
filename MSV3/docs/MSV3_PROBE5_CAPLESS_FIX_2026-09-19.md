# Probe 5: pre-capless firmware on C17-removed hardware, and the fix (2026-09-19)

**Status: fixed and verified. Probe 5 runs signed capless v65564 and is producing valid readings.
One physical caution remains open (it is on USB in a pot with a known mains-coupling history).**

## What happened

The maintainer placed a "classic MSV3" probe into the shared Alocasia pot on 2026-09-19 to fill the
MSV3-blade + FDC corner of the electrode/readout comparison. It was assumed to be probe 8. **It is
probe 5** (`F9:18:9B:CA:10:B2`, matches the registry). Probe 8 remains `deployed: false` and unseen.

Probe 5 had been dark since 2026-09-14 03:05 UTC. It came back on the air still running pre-capless
**v65555** and its first rows were unusable:

| field | 2026-09-14 (last prior) | on return, pre-fix |
|---|---:|---:|
| `raw_fdc_counts` | 19,850,210 | 36,221,528 |
| `bare_tank_fdc_counts` | 22,095,137 | **65,546,070** |
| `moisture` | 41,498 | **0** |
| `moisture_scale` | 2 | 2 |

**`moisture` computed to zero.** Two causes, both from the same mismatch:

1. `moisture_scale` 2 is the pre-capless scale; the capless calibration is scale 3.
2. A pre-capless image still measures a **bare-tank reference that C17-removed hardware no longer
   has**. That measurement landed at 65.5M counts, i.e. above the FDC2214's 10 MHz sensor ceiling and
   the 8.75 MHz FIN_SEL=01 range.

The second is **the same out-of-range failure mode as probe 4's v65566 block**, whose 18 rows were
deleted from `msv2_sensor_history` the same day. Same root cause in both: a build measuring a tank
configuration the hardware no longer has.

## C17 removal confirmed by measurement on this board

Probe 5's counts moved from the **~19.85M** band on 09-14 to the **~36.2M** band on return. That is
the C17-installed -> C17-removed regime shift, matching probe 8's measured split of ~20.49M with C17
installed and ~39.41M with it removed.

This is independent corroboration of the "C17 is physically removed from every probe" record, on a
board that had not been separately checked. The firmware simply had not been updated to match.

## The fix

`MSV3/bootloader/generated/1.0.3/msv3-1.0.3-sensor005-v65564-signed-ota.zip`, build
**`2026-09-14-capless-prod5`**. It had been built on 2026-09-14 alongside probe 6's capless image and
never installed.

Preflight passed all gates, with no override flags needed (v65564 clears probe 5's rollback floor of
65555):

```text
integrity    OK -- signature verifies against the key compiled into msv3-1.0.3, device_type 0x3356
boot guard   OK -- guard 2 ON, bootloader msv3-1.0.3 reserves 0x2003FFF8
identity     OK -- MSV3-MOISTURE-005 matches probe 5
```

Installed over BLE through the resident 1.0.3 bootloader. Pre-jump DIS reported the outgoing build as
`2026-09-08-prod5`. **Transfer completed in 32 s**, 4.02 kB/s, MTU 247.

## Verification

**DIS proven without taking the probe off the air.** A deliberate same-package re-run was REFUSED by
the already-running-build guard before any jump, reading back `2026-09-14-capless-prod5`. This is the
same technique used on probe 6 on 2026-09-14 and is the preferred way to confirm a build ID.

**200 s guarded scan** (`msv3_fleet_scan.py --probe 5 --require-progress`): 34 packets, mean -71 dBm,
wake advancing, 4310 mV, die 21.75 C, `DATA_CH0` 35,490,875, bare invalid, OTA gate open,
flags **`0x54`** -- capless bit 6 **SET**, `bareValid` CLEAR, `invalid` CLEAR. Result: *all requested
probes healthy*.

**Cloud rows confirm the switch end to end**, visible row by row across the reset:

| wake | `moisture_scale` | `bare_tank_fdc_counts` | `moisture` |
|---:|---:|---:|---:|
| 8 (pre-fix) | 2 | 65,478,806 | 0 |
| 9 (pre-fix) | 2 | 65,562,392 | 0 |
| 1 (post-fix) | **3** | **NULL** | **44,399** |
| 4 (post-fix) | **3** | **NULL** | **44,564** |

`MSV3/fleet.json` `installed_application_version` updated 65555 -> 65564.

## The phase-gap alarm was insertion transients, not a fault

A scan taken **while the probe was being inserted into the pot** reported phase 1 at 152,328,492
against phase 0 at 36,335,033 -- a **115,993,458** gap, tripping the analog-path warning. That is the
same signature as probe 7's cracked TMUX joint, which put phase 1 about 15,000,000 above phase 0.

After settling, the gap is **47,934**, far below the 1,200,000 threshold. The database rows from the
same minutes showed phase 1 at 36,221,528, right next to phase 0, contradicting the scan.

**Do not scan a probe while handling it.** A single-packet phase sample taken mid-insertion can
manufacture a convincing hardware-fault signature. Re-scan after the board has settled before acting
on a phase-gap warning.

**This is not a clean bill of health.** One settled reading does not exclude an *intermittent* fault,
which is what a cracked joint looks like. Repeat the stationary scan several times over the coming
days before treating probe 5's analog path as proven.

## Open caution: USB power in a pot with a mains-coupling history

Probe 5 is currently **USB powered**, so its ~4300 mV battery telemetry is **not a cell measurement**.

More importantly, the shared Alocasia pot's AM rig (sensor 3) previously showed cyclic noise bands
that were traced to **mains coupling from the receiver PSU sharing an outlet with the bench rig**. The
quantity probe 5 is being deployed to measure is a **sub-1 % daily thermal wobble**.

**Move probe 5 to battery, or at minimum to a different outlet from the receiver, before its data is
used for the electrode-versus-readout comparison.** This is a physical change and has not been made.

## Consequence

All four corners of the electrode/readout matrix are live for the first time:

| | MSV3 blade | v1.2 electrode |
|---|---|---|
| **AM detector** | sensor 9 | sensor 3 |
| **MSV3 FDC** | **probe 5** (new) | probe 4 |

Probe 5 has no clean data yet. See `MSV3_ELECTRODE_VS_READOUT_THERMAL_2026-09-19.md` for what the
matrix is meant to resolve and why the comparison is still blocked on the AM detector's mV-per-pF
calibration.
