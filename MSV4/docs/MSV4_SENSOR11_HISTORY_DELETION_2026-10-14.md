# Audit: 1 row deleted from `msv2_sensor_history`, sensor 11 (2026-10-14)

**This is the record of a destructive change to the production cloud history**, made at the
maintainer's request on 2026-10-14 (~15:15 PDT): "can you remove it from history as well, I need
clean plots of this".

**DURABILITY WARNING.** The backup CSV is under `data/sensor_logs/`, which is gitignored
(`data/sensor_logs/*.csv`). It exists only on this machine until it is copied somewhere durable.

## What was deleted and why

One row: `receiver_id = 'MSV2088A3104A7AC'`, `sensor_id = 11` (MSV4 board 2), `id = 803694`,
`observation_id = 'E18FE1DD-00000474'`, wake 210 of the v262168 boot, received
**2026-10-14 22:07:17.261 UTC (15:07:17 PDT)**.

That row's fit gave Rsoil 15,794 ohm, Cm 15.0 pF, Cd 17.51 pF and tip 22.25 C. The maintainer
stated it was measured while the probe was out of the water between moves: the third pinch of
salt went into the cup at that moment (cup-of-water test, see `MSV3/fleet.json` msv4_boards
board 2 `active_test`).
- **Before (wake 209, 15:06):** Rsoil 202 ohm, in water.
- **After (wake 211, 15:08):** Rsoil 196 ohm, in water.
- **The rest of the run:** wake 210 is the only reading that is neither in air (12:09-12:39)
  nor in water. It was a wet blade in air for one sweep.

Stated justification: it describes no medium in the test and would put a spike in every plot of
the run. It is preserved in the backup, not discarded.

## Backup

```text
path    data/sensor_logs/msv4_sensor11_deleted_outofwater_2026-10-14.csv
sha256  21b64b67428bec0662daa2573a7cefe5b9d062f95726fdb02e37d6acb6e0a20d
rows    1 data + 1 header, all 21 columns including id and observation_id
```

## Exact deletion statement

```sql
delete from public.msv2_sensor_history
 where id = 803694
   and receiver_id = 'MSV2088A3104A7AC'
   and sensor_id = 11
   and wake = 210
   and observation_id = 'E18FE1DD-00000474'
   and raw_fdc_counts = 157940
 returning id, received_at, wake, observation_id, raw_fdc_counts;
```

It returned exactly that one row. A read of 22:05-22:09 UTC afterwards held wakes 208, 209 and 211
(ids 803684, 803689, 803697), untouched.

## Where it no longer appears

- **History page:** the app's History page reads this table through the `plant-dashboard`
  function's `share-history` RPCs (`msv2_read_share_sensor_history_*`); there is no other copy.
- **CSV fetches:** `cloud/scripts/fetch_supabase_history.py` reads the same table. CSVs fetched
  before the deletion still hold the row:
  - `msv4_sensor11_cuptest_2026-10-14.csv`
  - `msv4_sensor11_salt2_2026-10-14.csv`

## Restore procedure

The backup carries the original primary key. Verify the SHA-256, load the CSV into a staging table
with the column types of `public.msv2_sensor_history`, and insert it back by explicit column list.
