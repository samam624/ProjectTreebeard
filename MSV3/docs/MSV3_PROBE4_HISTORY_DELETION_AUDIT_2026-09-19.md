# Audit: 18 rows deleted from `msv2_sensor_history`, sensor 4 (2026-09-19)

**This is the record of a destructive change to the production cloud history.**

**DURABILITY WARNING.** The backup CSV lives under `data/sensor_logs/`, which is **gitignored**
(`.gitignore:12`, `data/sensor_logs/*.csv`). It therefore exists **only on the local filesystem** and
will not survive a fresh clone, a machine loss or a working-tree clean. **This note is currently
untracked too** -- it is in the uncommitted working tree, not in git history. Until both are
committed (or the CSV is copied somewhere durable and its checksum verified against the value below),
the deleted rows are recoverable only from this one machine. Commit this note, and store the CSV
outside the ignore rule if the rows are worth keeping.

## What was deleted and why

18 rows, `receiver_id = 'MSV2088A3104A7AC'`, `sensor_id = 4`,
**2026-09-18 23:39:31.07 → 23:59:21.101 UTC** (16:39:31 → 16:59:21 PDT), wake 1→18,
`raw_fdc_counts` 65,219,312 → 65,270,261.

These are the complete v65566 `hybridv12` block. That build measured on **mux code 2** with the
electrodes disconnected, leaving the tank at approximately **10.5 MHz** -- above the FDC2214's 10 MHz
sensor ceiling and above the 8.75 MHz FIN_SEL=01 range.

**Stated justification: invalid for moisture analysis, because the readings are outside the sensor's
datasheet operating range.** Note the deliberately narrow wording. These rows were *not* worthless in
every sense -- they retain diagnostic provenance about what the tank did in that configuration, which
is why they were preserved rather than discarded. The deletion was a data-hygiene action to stop an
out-of-range block being silently pooled into thermal analysis, not a judgement that the rows carried
no information.

## Block context

Boundaries fixed by wake-counter resets, 2026-09-18, PDT:

| block | window | rows | counts | disposition |
|---|---|---:|---|---|
| A | ..16:26:40 | 28 | ~37.43M | kept |
| B | 16:26:49–16:29:56 | 4 | 22.8–37.5M | **kept** (handling transient, meaningless but retained) |
| C | 16:30:36–16:38:46 | 7 | ~37.63M | kept |
| **D** | **16:39:31–16:59:21** | **18** | **65.2M** | **DELETED** |
| E | 17:00:10–20:52:20 | 199 | ~38.2M | kept (in range, but bench not pot) |
| F | 20:53:20 → | 1,194+ | ~36.5–36.9M | kept (the reverse-hybrid series proper) |

## Backup

```text
path    data/sensor_logs/probe4_deleted_v65566_outofrange_2026-09-19.csv
sha256  65e2abc88768b50ad2aeb933f60caeae42dd4e283b108b2ab25cd1cfb3b6e986
rows    18 data + 1 header
```

All 21 columns captured, **including the primary keys** `id` and `observation_id`:

`age_ms, bare_tank_fdc_counts, battery_percent, board_temp_quarter_c, display_moisture, id, moisture,
moisture_scale, observation_id, raw_battery_mv, raw_charging, raw_fdc_counts, raw_moisture_khz,
received_at, receiver_id, rssi, sensor_id, tank_phase,
temperature_compensated_log_ratio_ppm, temperature_compensation_model_version, wake`

## Exact deletion predicate

```sql
delete from public.msv2_sensor_history
 where receiver_id = 'MSV2088A3104A7AC'
   and sensor_id = 4
   and received_at >= '2026-09-18 23:39:31+00'
   and received_at <= '2026-09-18 23:59:22+00'
   and raw_fdc_counts > 50000000        -- guard: neighbours are 37-38M, cannot be hit
 returning received_at, wake, raw_fdc_counts;
```

The `raw_fdc_counts > 50000000` guard was deliberate: it makes the statement structurally incapable
of touching the adjacent in-range blocks even if the timestamp bounds were wrong.

## Restore procedure

The rows carry their original primary keys, so a straight re-insert restores them exactly:

1. Verify the backup against the SHA-256 above.
2. `\copy` the CSV into a staging table with the same column types as
   `public.msv2_sensor_history`.
3. `insert into public.msv2_sensor_history select * from <staging>;` — column order must be taken
   from the CSV header, not assumed.
4. Confirm with
   `select count(*) from public.msv2_sensor_history where sensor_id=4 and raw_fdc_counts > 50000000;`
   → expect 18.

## The 17-versus-18 discrepancy

The maintainer approved an option labelled **"Block D only (17 rows)"**. The block is actually **18
rows**, and 18 were deleted.

The count of 17 came from a first scoping query bounded `received_at <= '2026-09-18 23:59:21+00'`,
which clipped the final sample at `23:59:21.101`. Deleting 17 would have stranded one 65.2M row in
the middle of otherwise-valid data, which is plainly not what "block D only" meant. The discrepancy
was surfaced to the maintainer at the time rather than applied silently.

**Lesson: bound timestamp ranges on a sub-second-precision column with strict inequalities against
known-good neighbours, not with truncated literals.** The safe form here would have been
`received_at > '2026-09-18 23:38:46.686+00' and received_at < '2026-09-19 00:00:10.092+00'`.

## Verification after the delete

- `select count(*) ... where sensor_id=4 and raw_fdc_counts > 50000000` → **0**, all dates.
- Seam contiguous: wake 117 / 37,627,768 at 23:38:46.686 now directly precedes wake 1 / 38,273,571 at
  2026-09-19 00:00:10.092.
- Surviving blocks intact: E = 199 rows (38.17–38.28M), F = 1,194 rows (36.52–36.92M).
- Sensor 4 total 27,395 rows spanning 2026-08-23 → 2026-09-20; all pre-09-18 production history
  untouched, including the eight-day solar-charging baseline and the −0.387 %/C thermal dataset.

## A failed first attempt left no partial state

The first attempt used a `DO $$ ... $$` block with a row-count guard that would `raise exception` on
anything other than 18. It failed on shell quoting **before reaching the server**. A count query
confirmed all 18 rows still present, so there was no partial delete. The successful attempt used
`DELETE ... RETURNING`, which enumerated exactly what it removed.

## Related firmware change the same day

Probe 5 was moved from pre-capless v65555 to signed capless v65564 — the same underlying fault class
(a build measuring a tank configuration the hardware no longer has). Recorded in
`MSV3_PROBE5_CAPLESS_FIX_2026-09-19.md` and in `MSV3/fleet.json`.
