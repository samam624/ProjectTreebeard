# MSV3 24-hour water immersion and air recovery (2026-09-03)

**Result: clean.** A 24-hour full immersion in a cup of water produced a ~5x change in
electrode-contributed capacitance, and removal to open air returned the probe to its pre-soak
baseline with no resolvable recovery tail. The bare-tank reference channel held flat across the
entire cycle, which is the first validation of that channel against a large deliberately-forced
step rather than against ambient drift.

## Setup

- One MSV3 probe reporting as sensor ID **4** (`MSV3-MOISTURE-004`), running
  `MSV3/arduino/MSV3_Moisture_BLE_Sleep/`, telemetry via the DisplayCenter receiver to the cloud
  history at the production 1-sample-per-minute advertising cadence
  (`ADVERTISE_INTERVAL_MS = 60000`).
- **Corrected 2026-09-03:** this record originally named sensor 5. It is sensor 4, established
  against `msv2_sensor_history` rather than from the chart: across 2026-09-02..04 sensor 4 spans
  raw `15,284,233` to `20,356,194` -- the immersion step and the air baseline quoted below -- while
  sensor 5 never leaves `18,834,311..19,551,329` and was never immersed. Every measured value in
  this document is sensor 4's and is unaffected; only the board identity was wrong.
- Blade fully immersed in a cup of tap water for ~24 hours, then lifted out and left sitting in
  open air. No soil at any point.
- Chart window reviewed: 2026-08-31 16:53 through 2026-09-03 16:50. Immersion began ~Wed
  2026-09-02 early afternoon and ended ~Thu 2026-09-03 early afternoon; edges read off the History
  chart, exact timestamps recoverable from the stored history rows.
- **Coating state of this board at test time: not recorded.** This materially changes how far the
  ingress conclusion below generalizes — fill this in before citing it as a coating result.

## Measured

Normalization is `msv3ReferenceExcessPercent()` in
`DisplayCenter/arduino/Treebeard_ESP32_Display_Runtime/MobileAppPage.h` — `(Dbare/Dconnected)^2 - 1`,
reported as percent capacitance above the bare tank.

| State | Raw connected `DATA_CH0` | Bare-tank reference | Normalized |
|---|---|---|---|
| Pre-soak, air | ~20.35M | ~22.63M | ~23-24% |
| Immersed (24h plateau) | ~15.33M | ~22.63M | ~118% |
| Post-soak, air (latest) | 20,341,560 | 22,632,650 | 23.8% |

Latest row checks out exactly against the formula: `(22632650 / 20341560)^2 - 1 = 23.795%`.

The excess-over-bare term goes from `0.238 * Cbare` dry to `1.18 * Cbare` immersed — **a 4.96x
change in the electrode-contributed capacitance**, ~94 percentage points of normalized swing.

## Findings

### 1. No recovery tail — no detectable water ingress

The removal edge is as vertical as the entry edge: baseline is reached within roughly one sample
(one minute). For a water cup this is the physically expected answer — there is no porous medium
holding water against the blade, so the bulk-water dielectric leaves as soon as the probe does —
but the *absence of a slow multi-hour decay* is the informative part. Water that had wicked into
the FR-4 laminate or under the soldermask would dry out over hours and show as an exponential tail
on the connected channel. Over 24 hours of full immersion, none appeared.

This addresses the ingress concern raised in
`MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md` for the water case specifically. It does
**not** substitute for the scrap-FR-4 coupon soak that document calls for before declaring the
832HD-under-419D edge stack production-qualified — that test is about long-term wet ionic soil, and
one 24-hour clean-water immersion is neither long-term nor ionic.

### 2. No conflict with the ~360 pF dried-after-wet soil plateau

The 2026-08-31 entry in `docs/STATUS_HANDOFF.md` recorded fresh/never-wet dry at ~50-60 pF and a
"dried after wet" plateau at ~360 pF — residual moisture, not a fault. Nothing here contradicts
that. That plateau was measured in soil, where residual water persists in the medium against the
blade. This test has no medium. Full baseline return in water is not evidence that the soil
plateau was wrong.

### 3. The bare-tank reference channel is validated

The bare-tank reading (mux codes 2/3, electrodes disconnected) stayed within 22.62-22.64M for the
entire three-day window — no step at either edge — while the connected channel moved 4.6M counts.
That is direct evidence the swing lived in the electrode path and not in a board-wide parasitic,
supply, or thermal shift. Until now this channel had only been checked against ambient drift; this
is the first time it has been held still against a large forced step in the connected channel.

The same point from the other direction: the bare-tank trace visibly tracks board die temperature
(inversely) across a ~6 degC diurnal swing, and the normalized trace stays flat through that swing
in both the wet and dry states. The ratio is cancelling thermal drift as designed — see
`MSV3_FDC_REFERENCE_COMPENSATION_2026-08-30.md` and
`MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md`.

### 4. Residual offset is below measurement resolution

Post-soak normalized is 23.8% against a pre-soak baseline reading ~23-24% off the chart. Any
residual is at or under ~1 percentage point against a ~94-point wet signal, which is not
resolvable at chart resolution and is not functionally meaningful. Worth a second look against the
stored rows if a precise residual number is ever needed.

### 5. Jagged board-temperature band on the final day is the AC

From roughly the early hours of 2026-09-03, board die temperature switches from smooth diurnal
swings to a ~2-3 degC sawtooth, and the bare-tank reference gets correspondingly jagged. **This is
room air conditioning cycling** (confirmed by the maintainer), not a sensor or firmware fault. The
two channels agreeing is the expected signature — bare-tank follows board temperature — and the
band began before the probe was removed from water, so it is unrelated to the immersion test.
Distinct from the mains-coupling noise bands on the Alocasia GP AFE rig, which are a different node
and a different mechanism.

## Not answered by this test

- The dry-versus-pre-soaked blade A/B in fixed-depth covered water, still open in
  `MSV3_MULTI_DAY_FDC_DRIFT_FOLLOWUP_2026-08-28.md`. This was a single blade against itself, not a
  matched pair.
- Anything about ionic/salty water or soil chemistry. Tap water only.
- The settle-versus-polarization timing window for the tank reading
  (`MOISTURE_SENSOR_FDC2212_FRONTEND.md` open item 7).
- Whether the conformal coat changes any of the above — see the unrecorded coating state noted in
  Setup.
