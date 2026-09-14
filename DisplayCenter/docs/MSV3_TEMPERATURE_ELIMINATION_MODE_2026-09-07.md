# MSV3 receiver-side temperature elimination mode (2026-09-07)

> **Legacy scale-2 reference only (superseded 2026-09-14).** Capless scale-3 probes have C17
> removed, never acquire a bare-tank value, and force this learned compensation channel off. Raw
> connected counts and board temperature remain available for a future soil/interface model. See
> `MSV3/docs/MSV3_CAPLESS_NO_BARE_TANK_POLICY_2026-09-14.md`.

The first implementation lives in DisplayCenter, not in the MSV3 probe. An MSV3 continues to
advertise the original connected FDC count, board (die) temperature, bare-tank FDC count, and tank
phase. DisplayCenter calculates and stores a separate derived signal, so the raw observation is
always available for re-analysis.

## Modes

- `off`: keep raw behavior and do not advance or emit the temperature model.
- `learning`: calculate and upload the corrected shadow channel, but keep the existing moisture
  value on the physical display and in alerts. This is the default for old and new configurations.
- `active`: request corrected presentation. It remains effectively `learning` until two accepted
  24-hour fits have been observed; only then can corrected equivalent moisture feed the existing
  centered presentation filter and alert path.

The local Setup tab stores the choice at `card.moisture.temperatureCompensation`. The Status tab
shows the receiver's effective mode and confidence.

## Model v2 (2026-09-09)

For connected count `Dm` and same-packet bare count `Db`, the base observable is:

```text
S = 2 * (ln(Db) - ln(Dm)) = ln(Cconnected / Cbare)
```

Model v2 uses a two-hour EMA of the on-board die temperature and a 48-hour EMA of `S`. Probe 4 is
seeded from the latest chronological September 6-9 fit with thermal gain `0.01568 / degC` per unit
of capacitance excess and calibration reference `22.34 degC`. Probes without their own fit retain
the conservative fleet seed `0.01254 / degC` and `22 degC` reference. Each daily online regression
includes an intercept, linear dry-down
term, thermal term, and tank-phase offset. A fit is accepted only with at least 120 readings, at
least 2 degC of effective-temperature span, R-squared of at least 0.40, and bounded coefficients.
Accepted coefficients are blended 25% at a time.

EMA state, the complete partial-regression sufficient statistics, elapsed observed training time,
calibration reference, accepted fits, and model epoch are checkpointed to `/temp-comp-v2.json`
every 15 minutes outside the BLE callback. A receiver reboot restores the block and resumes its
observed duration; unobserved downtime is not counted. A probe wake-counter restart, a connected
count step above 3%, or a bare-reference step above 0.35% starts a fresh epoch and returns an
explicitly requested Active mode to effective Learning until two new fits pass. These thresholds
are intentionally above Probe 4's measured ordinary watering response and below its physical
insertion/removal and cup-immersion steps.

The stored output is `temperature_compensated_log_ratio_ppm`, equal to corrected `S * 1,000,000`.
The History tab reverses that representation to a directly readable quantity:

```text
capacitance above bare (%) = 100 * (exp(ppm / 1,000,000) - 1)
```

This makes its vertical units identical to the existing raw bare-reference normalized chart. The
History view now overlays bare-normalized samples, corrected samples, and a causal 12-hour trailing
median on one y-axis. It plots only the latest model/physical epoch, so independent autoscaling and
old immersion/reinsertion steps cannot masquerade as the current moisture trend. The median is the
slow trend display; use the corrected samples, not the delayed median, when validating watering
step timing.

## Cloud path

Migration `040_msv3_temperature_compensated_history.sql` adds nullable compensated-value and model-
version columns, updates batch/legacy ingestion, and exposes both columns through every history
reader. `receiver-snapshot` validates the derived channel independently; a bad derived value is
stored as null without discarding valid raw measurements from the row.

## Rollout

1. Apply migration 040.
2. Deploy the updated `receiver-snapshot` Edge Function.
3. Flash the guarded DisplayCenter build.
4. Leave each MSV3 card in `learning` for at least two accepted daily fits and compare the corrected
   samples and 12-hour trend against the same-axis bare-normalized trace plus the connected,
   bare-tank, and temperature charts.
5. Keep Probe 4 in `learning` until a deliberately recorded watering verifies that the corrected
   fast signal preserves watering magnitude and timing. Only then select `active` per plant.

This model removes the repeatable receiver-observed thermal component. It does not claim to measure
soil temperature or to remove genuine temperature/conductivity effects in soil; a real soil
temperature sensor is still the right hardware reference for a later model revision.
