-- MSV4 raw sweeps (2026-10-15). A probe from application version 262172 puts the 8-point averaged
-- transfer its moisture fit was made from in its scan response (layout: the comment above
-- SWEEP_RSP_LEN in MSV4/arduino/MSV4_Moisture_BLE_Sleep/MSV4_Moisture_BLE_Sleep.ino). The receiver
-- forwards the latest one per sensor as plants[].raw.msv4.sweep, and receiver-snapshot stores it
-- here through msv4_store_raw_sweeps(). msv2_sensor_history keeps only the firmware's fitted values;
-- these rows let other electrode models be fitted to a soil record
-- (MSV4/docs/MSV4_BOARD2_RAW_SWEEP_MODELS_2026-10-15.md).
--
-- sweep_hex is the probe's 24 packed bytes as sent, lower-case hex: per point (250 kHz .. 2.67 MHz)
-- the transfer's real and imaginary parts as signed 12-bit integers in 1/2048 of drive. The
-- receiver sends the same sweep on every upload until the next one replaces it, so the unique
-- key makes every repeat a no-op; a wake number reused after a probe reboot carries a different
-- sweep and is kept.

create table if not exists public.msv4_raw_sweeps (
  id bigint generated always as identity primary key,
  receiver_id text not null references public.msv2_receivers(receiver_id) on delete cascade,
  sensor_id integer not null check (sensor_id between 1 and 16),
  wake integer not null check (wake >= 0),
  sweep_hex text not null check (sweep_hex ~ '^[0-9a-f]{48}$'),
  observed_at timestamptz not null,
  received_at timestamptz not null default now(),
  unique (receiver_id, sensor_id, wake, sweep_hex)
);

alter table public.msv4_raw_sweeps enable row level security;

create index if not exists msv4_raw_sweeps_receiver_sensor_time_idx
  on public.msv4_raw_sweeps(receiver_id, sensor_id, observed_at desc);

-- p_rows: [{"sensor": 11, "wake": 1234, "hex": "<48 hex>", "observedAtMs": 1760570000000}, ...],
-- already validated by receiver-snapshot's raw-sweeps.ts. Returns the number of NEW rows.
create or replace function public.msv4_store_raw_sweeps(
  p_receiver_id text,
  p_rows jsonb
)
returns integer
language plpgsql
security definer
set search_path = public
as $$
declare
  inserted integer;
begin
  insert into public.msv4_raw_sweeps (receiver_id, sensor_id, wake, sweep_hex, observed_at)
  select
    p_receiver_id,
    (r->>'sensor')::integer,
    (r->>'wake')::integer,
    lower(r->>'hex'),
    to_timestamp((r->>'observedAtMs')::double precision / 1000.0)
  from jsonb_array_elements(p_rows) as r
  on conflict (receiver_id, sensor_id, wake, sweep_hex) do nothing;
  get diagnostics inserted = row_count;
  return inserted;
end;
$$;

-- Postgres grants EXECUTE to PUBLIC by default at function creation (see 012's note).
revoke execute on function public.msv4_store_raw_sweeps(text, jsonb) from public;
grant execute on function public.msv4_store_raw_sweeps(text, jsonb) to service_role;
