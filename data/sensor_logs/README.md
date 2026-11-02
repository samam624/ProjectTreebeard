# data/sensor_logs

The cloud (Supabase) is the store. Nothing here is a source of truth.

- `cache/` (gitignored): ONE file per sensor, e.g. `sensor10_raw.csv`, grown in place. Refresh with
  `python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 10 --raw --cache`
  (first run fetches everything stored, later runs append only newer rows).
- `latest/`: the plots, one fixed filename each, overwritten on every run. Older versions are in git history.
- `archive/` (gitignored): old dated snapshots and one-off exports, kept only until deleted by hand.
- Other files: dated analysis outputs and figures cited by docs. Do not add a new dated CSV or PNG per request;
  put a conclusion in the owning subsystem's `docs/` instead.
