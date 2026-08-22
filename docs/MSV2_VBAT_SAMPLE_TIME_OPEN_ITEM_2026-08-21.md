# MSV2 `readBatteryMv()` — possible undersampling while charging (open item)

**Status: not confirmed, not touched. Deferred while focus stays on MSV3 bring-up.**

## What prompted this

While bench-testing MSV3 on 8/21, `vbat` read ~5.3-5.6V (raw ADC) against a true ~4.2-4.3V
battery (multimeter) — but only while USB/charging was active; it matched the multimeter fine
at rest. Root cause: `readVbatMv()` (`arduino/MSV3_Probe_Bringup_Test/MSV3_Probe_Bringup_Test.ino`)
drove the divider's ground-return pin low and waited only 5ms before reading — exactly *one*
R29‖R30/C16 time constant (100k‖100k × 100nF = 5ms), i.e. only 63% settled, not the ~99% needed.
Confirmed by holding the divider low with a new `vbathold <secs>` command and probing it directly
with a multimeter: the physical divider was correct (VBAT_DIV read right at VBAT/2), so the bug
was entirely in the firmware's settle time, not the hardware. Fixed by extending the delay to
25ms (5 time constants) — verified afterward, predicted 4.298V vs. actual 4.25V, within margin.

## Why MSV2 might have a *related* issue

`readBatteryMv()` in `arduino/MSV2_Moisture_BLE_Sleep/MSV2_Moisture_BLE_Sleep.ino` (and the same
function duplicated in `arduino/MSV2_Moisture_RC_Test/MSV2_Moisture_RC_Test.ino`) reads the XIAO
nRF52840 Sense's onboard VBAT divider (R1=1M, R2=510k → Thevenin ≈ 338kΩ, about 6.7× MSV3's 50kΩ)
and never calls `analogSampleTime()` — it stays on the SAADC core default of 3µs acquisition time.
MSV3's own fix comment already establishes that 3µs is inadequate at just 50kΩ source impedance
(that's why MSV3 explicitly sets `analogSampleTime(40)`, the longest available). MSV2's source
impedance is substantially higher, on the same default 3µs.

This is a **different mechanism** than the MSV3 bug (acquisition time, not pre-read settle delay
racing a deliberate filter cap) — there's no evidence of an equivalent large capacitor on the
XIAO's onboard divider, so the specific "RC comparable to delay" failure mode probably doesn't
carry over directly. But an inadequate acquisition time at a high source impedance is a
structurally analogous risk via a different part of the same read path.

Also unknown: whether `VBAT_DIVIDER_COMP = 2.946` (the one-time multimeter calibration constant,
referenced in both files) was derived while the battery was actively charging (solar or USB) or
only at rest. If only validated at rest, the same blind spot that hid the MSV3 bug applies here
too — a charging-specific error wouldn't have shown up in that calibration.

## Next steps (whenever this gets picked up)

1. On a running MSV2 unit, compare its battery reading against a multimeter **while actively
   solar-charging**, same way the MSV3 bug was caught. If it's already accurate, this is a
   non-issue and can be closed without a code change.
2. If a discrepancy shows up, try adding `analogSampleTime(40)` to `readBatteryMv()` (both
   copies) as the first thing to test — cheap, matches the reasoning already validated on MSV3,
   and costs nothing measurable since this read happens once per wake.
3. Not urgent, but worth knowing: this firmware is currently deployed and monitoring real plants,
   so any real error here would be silently affecting battery-percent reporting specifically
   during daylight/charging hours, which is a meaningful fraction of a deployed unit's time.
