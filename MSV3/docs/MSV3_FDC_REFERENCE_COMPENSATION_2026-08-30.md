# MSV3 bare-tank reference compensation

**Date:** 2026-08-30  
**Status:** Physics-based first stage implemented and flashed to the DisplayCenter as an experimental
History trace; production moisture, alerts, smoothing, auto-calibration, probe firmware packets, and
stored raw fields unchanged.

> **2026-09-07 terminology and validation update:** Probe 4's current connected history is valid
> real-pot data: a genuine drying signal with thermal loading, not merely a temperature trace. The
> preferred user-facing name for this observable is **reference-normalized moisture signal
> (experimental; higher = wetter)**. A causal second-stage model trained on 48 hours reduced the
> held-out final day's daily component by 74% after this reference normalization while preserving
> the multi-day dry-down. See
> [the real-pot compensation result](MSV3_REAL_POT_THERMAL_COMPENSATION_2026-09-07.md).

> **Later same-day physical context:** Water pockets were found underneath the blade's nail-polish
> coating. Removing the polish caused the earlier long drift to reverse during drying and then settle
> to a flat air baseline. The reference analysis below remains valid for separating common tank
> motion, but the coating finding is now the leading explanation for the large in-soil residual. See
> [MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md](MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md).

## Implemented observable

For every history row containing a valid moisture-connected count `Dm` and its paired disconnected
bare-tank count `Db`, the receiver-served browser computes:

```text
reference-normalized moisture signal (%) = 100 * ((Db / Dm)^2 - 1)
```

The FDC2214 reports `D = 2^28 * fsensor/fref`, while a resonant tank has
`fsensor = 1/(2*pi*sqrt(L*C))`. Therefore:

```text
(Db / Dm)^2 = Cconnected / Cbare
```

when the two close-in-time readings share the same reference clock and inductor. Multiplicative
movement in the FDC reference clock and inductor cancels without a fitted coefficient. The value is
dimensionless, has no time-window-dependent baseline, and preserves watering direction: additional
capacitance lowers `Dm` and raises the normalized value.

TI uses the same general principle in its FDC2214 liquid-level example: environmental and liquid
reference electrodes are combined ratiometrically rather than relying on an unreferenced high-
resolution count. See the [FDC2214 datasheet, typical application](https://www.ti.com/lit/ds/symlink/fdc2214.pdf).

The new trace is deliberately labeled **experimental** and is not called water-content percent.
The bare reading cannot see temperature-dependent soil permittivity, conductivity, probe coating,
or contact geometry. Published capacitance-probe work likewise separates resonant-circuit calibration
from soil-specific dielectric/loss calibration and warns that salinity and temperature affect the
frequency response. See [Kelleners et al., USDA-ARS](https://www.ars.usda.gov/arsuserfiles/20361500/pdf_pubs/P1878.pdf)
and [Seyfried and Grant](https://digitalcommons.unl.edu/icwdm_usdanwrc/1065/).

## First live window

The initial complete diagnostic window queried from Supabase contained 269 paired rows from
2026-08-30 12:06 PDT through 17:20 PDT. During that short window:

- die temperature ranged from 20.75 to 28.25 degrees C;
- bare tank ranged from 22,609,494 to 22,637,554 counts;
- moisture-connected tank ranged from 18,369,918 to 18,593,962 counts;
- bare count versus die temperature correlation was `-0.9946`;
- the bare reference's fitted slope was about `-3,580 counts/degree C`;
- connected count's fitted slope was about `-6,142 counts/degree C`;
- a count-equivalent bare normalization reduced that slope to about
  `-3,223 counts/degree C`, a 47.5% reduction;
- temperature correlation of the connected-capacitance proxy fell from `+0.260` to `+0.140`;
- the phase-adjusted log-ratio offset was about 211 ppm, approximately 0.021%; and
- phase pairing reduced local roughness by 15.9%, pair-to-pair standard deviation by 12.1%,
  and median absolute pair change by 7.8% in this first window.

The latest screenshot pair, `Dm=18,474,883` and `Db=22,635,479`, produces approximately
`50.1122% capacitance above bare`.

This proves that the bare reference is useful: it is tracking front-end thermal motion extremely
closely and removes roughly half of the connected count's apparent temperature slope in the first
window. It also proves that it cannot do all the work. The remaining connected/bare ratio contains
real electrode/soil/coating movement, insertion settling, and possibly soil-temperature response.
One five-hour heat/cool segment with no held-out watering event is insufficient to identify those
terms safely. For scientific exports, pair adjacent opposite phases in log space when the data are
full-density and close in time:

```text
Sphase = 2 * (ln(Db) - ln(Dm))
Spair = (Sphase0 + Sphase1) / 2
      = ln((Db0 * Db1) / (Dm0 * Dm1))
```

The browser trace remains same-row rather than phase-paired because long History windows are
intentionally downsampled before reaching the browser. Pairing sparsely retained points across a
large time gap could smear a watering event. The measured phase offset is small and the same-row
ratio remains physically valid; phase-paired modeling belongs in the full-resolution analysis path.

## Why die-temperature subtraction is not enabled yet

The correct order is:

1. calculate the paired bare/connected ratio;
2. examine the ratio residual against temperature;
3. fit a small probe-specific temperature term only on constant-moisture calibration blocks;
4. freeze the coefficient and validate it on complete, previously unseen days and watering cycles.

An unconstrained regression over ordinary field history can delete the desired signal because die
temperature, daytime evaporation, long dry-down, and moisture redistribution are correlated. The
nRF52840 temperature is also MCU die temperature, not soil or LC-tank temperature. It may lead or lag
the relevant temperature and should initially be treated as a diagnostic covariate.

This is not only a theoretical warning. A chronological trial trained a linear die-temperature
coefficient on the first 60% heating interval and applied it to the later cooling interval. Because
temperature and slow connected-sensor movement had occurred together during training, the model
learned a false coefficient. On cooling it changed temperature correlation to `-0.990` and increased
first-difference noise from about 184 to 1,043 ppm—roughly **5.7 times worse**. Randomly mixing rows
between training and validation would have hidden this failure.

When enough data exist, fit in log-ratio space:

```text
Z = 2 * (ln(Db) - ln(Dm))
Zcorrected = Z - beta * (Teffective - Tref) - phaseOffset
```

Start with one linear coefficient. Add a causal temperature lag, quadratic term, or phase interaction
only when whole-day chronological validation improves. Do not use hour of day as a correction input.

Temperature correction for dielectric soil sensors is known to be probe-, soil-, frequency-, and
moisture-state-dependent. Published calibration work supports fitting temperature effects at fixed
water-content plateaus rather than learning one universal coefficient from an uncontrolled dry-down.
See [Chanzy et al.](https://pmc.ncbi.nlm.nih.gov/articles/PMC3444127/) and
[Nasta et al.](https://www.mdpi.com/1424-8220/24/3/952).

## Acceptance gates for the second stage

Collect at least several full daily cycles, preferably including repeated watering events, after the
current insertion transient has settled. For each candidate model compare:

- daily peak-to-peak and 24-hour spectral amplitude during no-watering periods;
- correlation with die temperature and bare reference, including causal lags;
- phase-0 versus phase-1 offset;
- noise measured by median absolute deviation, not only total range;
- held-out watering step, 1/6/24-hour response, timing, and signal-to-pre-event-noise ratio.

Use whole days and whole waterings as chronological holdouts. A correction is accepted only if it
reduces quiet-period thermal variation while preserving watering magnitude and timing. Nearby samples
must not be randomly split between training and validation; that would leak the same autocorrelated
event into both sets.

## Implementation boundary

The formula currently exists only in `MobileAppPage.h` and is calculated retroactively from same-row
raw fields. That is intentional:

- raw data remain the source of truth;
- historical rows automatically benefit as the display formula evolves;
- no BLE payload, ESP32 history queue, Supabase migration, or Edge Function change is needed;
- no unvalidated model can alter the physical display, alerts, or watering logic.

After multi-day validation, productionizing a corrected moisture value requires an explicit new
measurement-model version and a deliberate calibration migration. It must not silently redefine the
existing `moisture` column.

## Deployment

On 2026-08-30 the guarded DisplayCenter build compiled successfully and was uploaded to the connected
ESP32-S3 on `COM16` as build `MSV2 runtime build 2026-08-30 msv3-reference-normalization-v1`.
The uploader verified the flash hash and hard-reset the receiver. The MSV3 probe and cloud schema were
not reflashed or changed for this browser-derived trace.
