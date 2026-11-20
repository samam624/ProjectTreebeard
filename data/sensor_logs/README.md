# Sensor plots and data

## Latest plots

Open [the latest plots](latest/) for the most recent saved version of each recurring chart.

| Plot | Script |
| --- | --- |
| [Sensor 10 moisture candidates and pot weight](latest/sensor10_vs_mass.png) | [msv4_moisture_plot.py](../../MSV4/scripts/msv4_moisture_plot.py) |
| [Sensor 11 moisture and pot weight](latest/sensor11_mass_vs_moisture.png) | [msv4_begonia_mass_plot.py](../../MSV4/scripts/msv4_begonia_mass_plot.py) |
| [Stored moisture against weight for both sensors](latest/stored_vs_mass_s10_s11.png) | [msv4_stored_vs_mass_both.py](../../MSV4/scripts/msv4_stored_vs_mass_both.py) |

To refresh the first plot, run these commands from the repository root. The fetch uses the linked Supabase project and appends new rows to the local cache. The plot command also works offline with an existing cache.

```sh
python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 10 --raw --cache
python MSV4/scripts/msv4_moisture_plot.py
```

## Experiment records

| Experiment | Saved evidence |
| --- | --- |
| [Pot mass](studies/pot-mass/) | Weighings compared with moisture, capacitance and resistance |
| [Moisture model comparison](studies/model-comparison/) | Daily scores and comparisons across watering cycles |
| [Temperature](studies/temperature/) | Temperature coefficients, slope diagnostics and refits |
| [Water and salt cup test](studies/cup-test/) | Timeline, salt additions and fitted capacitances |
| [Raw sweeps, October 15](msv4_rawsweep_2026-10-15/) | Original bench captures, fitted sweep tables and three plots from that run |
| [Sensor 11 sweep fits](studies/sweep-fits/) | Model residuals, sweep coverage and a watering-cycle report |
| [Sensor 1 polarization check](studies/polarization/sensor1_polarization_check.png) | Dry plateau and watering response in the [November 16 analysis](../../MSV2/docs/MSV2_SENSOR1_POLARIZATION_CHECK_2026-11-16.md) |
| [Power and charging](studies/power/) | Battery traces, the dark-days report and the [November 19 charging comparison](studies/power/msv4_moisture_charging_2026-11-19.png) |

For a recurring chart, reuse its filename in `latest/`. Keep the figures and reports cited by a specific experiment together under `studies/` or with its original captures. Figures embedded in [MSV4 write-ups](../../MSV4/docs/) live in [MSV4/docs/figures/](../../MSV4/docs/figures/).

## Source data

- **Cloud history.** Supabase stores the uploaded readings. The local `cache/` holds growing per-sensor exports such as `sensor10_raw.csv` and derived tables such as `sensor11_sweep_fits.csv`. Both are ignored by Git.
- **Replay inputs.** The `moisture_5min_s<N>.csv` files in this directory contain five-minute medians. The [bucket fetcher](../../cloud/scripts/fetch_moisture_buckets.py) and [region replay](../../cloud/scripts/region_replay.mjs) use this location by default.
- **Local archive.** The ignored `archive/` holds one-off exports and backups of rows removed from Supabase. The deletion audits record the filenames and SHA-256 checksums for [sensor 4](../../MSV3/docs/MSV3_PROBE4_HISTORY_DELETION_AUDIT_2026-09-19.md) and [sensor 11](../../MSV4/docs/MSV4_SENSOR11_HISTORY_DELETION_2026-10-14.md).
- **Weighings and setup.** Pot mass logs and watering events used by the plotters are in the [fleet registry](../../MSV3/fleet.json). Use the registry for current hardware state.
