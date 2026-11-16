# Sensor 1 (MSV2 swapping probe): polarization check, 2026-11-16

**Question (maintainer):** sensor 1 still shows large polarization even with plate swapping. Is
the firmware the cause?

Physical state assumed from `MSV3/fleet.json` `non_msv3_nodes`: outdoors in the thaumatophilum
pot, on battery, with the MSV2 interdigitated electrode. That was last verified on 2026-09-30, so
it is assumed rather than confirmed.

## Firmware: `MSV2/arduino/MSV2_Moisture_BLE_Sleep/MSV2_Moisture_BLE_Sleep.ino`

This is the sketch the repo names for sensor 1. An MSV2 probe has no build-ID readback, so it is
NOT proven to be the image on the probe.

`countOscCycles()` holds one plate LOW, drives the other HIGH until it reads HIGH, then releases it
and times its RC decay to LOW through its own 10k pulldown. Then the two plates swap roles. Bursts
are 5 x 5 ms per 10 s wake, so the drive is on 0.25 % of the time.

Asymmetries:
1. **The swap balances cycle COUNTS, not volt-seconds.** Each half-cycle's charge into the soil
   depends on how long that plate stays high, which is set by its own R and C and its GPIO
   threshold. Plate B is a reworked jumper on P0.28, and plate A is the original trace on P0.02, so
   their capacitances differ. Neither the 10k parts nor the two thresholds are matched. Any
   difference leaves a net DC in the ratio (tA - tB)/(tA + tB). It is not measured, because the
   firmware sends only the total count.
2. A half-cycle that times out (5 ms) is still counted and still swapped.
3. `driveIsPlateA` is global, so an odd count in one burst carries into the next. There is no
   build-up from that.
4. Between bursts the last cold plate is left as an output driven LOW, and the other sits on its
   pulldown. Both are at 0 V.
5. Both plates only ever go positive against GND. If nothing else in the soil is grounded, that
   common mode has no return path through the soil.

## Data: cloud history to 2026-11-16 01:00, `data/sensor_logs/latest/sensor1_polarization_check.png`

The reading is the oscillator rate in kHz; lower is wetter. It moves in coarse steps of about
8-12 kHz.

- **The dry plateau does not move:** it reached 222 kHz on 10-13, 10-30 and 11-10/11-12, the same
  step each time. Polarization that builds with the drive would lower that plateau (read wetter)
  over a month of continuous measurement. It did not change by even one step.
- **Five sudden wet steps followed by days of drying:** 10-10 16:00, 10-22 08:00, 11-03 12:00,
  11-13 12:00 and 11-15 18:00. That is the shape water gives: in fast, out slow. Polarization would
  creep up slowly and release suddenly.
- **Two slow wetter creeps with no step:** 10-06 to 10-10 (203 to 178 kHz) and 10-16 to 10-22
  (222 to 159 kHz). These are the only stretches that look like the reported drift. Rain, a
  watering or a cold spell on those dates would explain them without any polarization; the record
  of those has not been given.
- The plateau was 247 kHz before 09-30 and 222 after, once the probe went outdoors. That is a
  change of place, not a drift.

**Maintainer, 2026-11-16:** the two slow creeps ARE the drift, and they come during long stretches
with no watering. A big watering, or rain (the probe is outdoors), resets them.

**Conclusion, revised.** The stable plateau shows only that nothing carries over a watering, not
that nothing builds up. The record fits drift that builds while the soil stays dry and is cleared by
water: the plateau holds for about 3 days (10-13 to 10-16), then creeps wetter for 6 days with no
water, and the next big wetting resets it. Water moving through the soil around the plates would
clear charge or ions piled up at their surface. The Cave Pearl Project reports the same limit
on its 555 soil probes: without polarity reversal, polarization limits the readings, and it gets
worse with slower pulses in dry soil
(https://thecavepearlproject.org/2020/10/27/hacking-a-capacitive-soil-moisture-sensor-for-frequency-output/).
Sensor 1 does reverse, but per cycle COUNT, not per volt-second (item 1).

**Firmware fix for item 1 (not built):** balance time instead of count. Time each half-cycle (the
loop already reads `micros()`), keep a running total of tA - tB, and give the next drive to the
plate that is behind. Send A and B separately at the same time, so the imbalance can finally be
measured. Whether that clears the drift is untested.
