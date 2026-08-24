# History Chart: Watering Snap False-Triggered by Single-Packet Noise (2026-08-23)

## Symptom

User-reported on the Alocasia GP History chart (Moisture, "Last 3 days"): the displayed
(smoothed) line tracked visibly below the center of the raw noise cloud for hours, then
jumped sharply upward for one step and slowly ratcheted back down over the following
packets - a "falls below center, then jumps up to compensate" pattern.

## Root cause

`smoothMoistureForDisplay()` in
`arduino/MSV2_ESP32_Display_Runtime/MSV2_ESP32_Display_Runtime.ino` (see
`docs/RECEIVER_MOISTURE_SMOOTHING_2026-08-22.md` for the full filter design) is
intentionally asymmetric: every dry-side (downward) move is guarded by an adaptive
deadband and a per-packet step cap, but any single packet whose rise over the current
displayed value cleared `wateringThreshold` snapped the display immediately, with no
confirmation step.

Alocasia GP's raw oscillator signal is unusually spiky (frequent large upward
excursions against a comparatively narrow baseline). Because ordinary upward jitter is
never blended in - only ever fully ignored or fully snapped - a run of noise with more/
larger upward spikes than downward ones lets the display drift below the cloud's true
center over time (nothing corrects it back up except a full watering snap). Once it had
drifted low enough, a single noisy outlier packet only had to cross the
(now easier to clear, since part of the threshold scales with the current displayed
value) watering threshold once to be misread as a real watering event, snapping the
display up; subsequent normal packets then had to be walked back down through the
capped dry-down ratchet, producing the visible spike-then-decay artifact.

## Fix

Require a watering-sized rise to persist for two consecutive unique packets before
committing to it, mirroring the filter's existing two-packet training gate and stalled-
wake hold-time pattern rather than introducing a new mechanism:

- First packet that clears `wateringThreshold`: display held unchanged,
  `SensorReading::moistureWateringPending` set true.
- Second consecutive packet also clearing the threshold: confirmed real watering, snap
  to the latest measurement immediately (unchanged end-user behavior for genuine
  watering events - one extra packet of latency).
- Any packet that does not clear the threshold while a candidate is pending: the
  candidate is discarded as a one-off outlier and that packet is evaluated normally
  (dry-down/deadband logic), instead of forcing a snap.

Dry-side behavior, span/deadband/noise-estimate math, and the stalled-wake escape hatch
are all unchanged.

## Status

Implemented and flashed. The later full-directory review also fixed the dry-side stalled-wake
counter so symmetric sub-watering noise cannot erase its progress. Receiver build
`2026-08-23 live-history-recovery-v2` was flashed to COM16 and `/device` confirmed the new tag.
Real traffic is flowing again; complete the 24-48-hour soak before calling the behavior fully
validated.
