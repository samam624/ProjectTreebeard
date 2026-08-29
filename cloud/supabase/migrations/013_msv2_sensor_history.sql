-- Adds durable, insert-only sensor reading history for overnight/long-run moisture
-- calibration tests. msv2_latest_snapshots (001) is upsert-only by design (receiver_id
-- primary key) for the live dashboard's "current status" use case - this is additive:
-- a separate insert-only table + RPCs, called once per plant per snapshot upload from
-- receiver-snapshot/index.ts, alongside the existing latest-snapshot upsert. Nothing about
-- the existing upload cadence or latest-snapshot behavior changes.

create table if not exists public.msv2_sensor_history (
  id bigint generated always as identity primary key,
  receiver_id text not null references public.msv2_receivers(receiver_id) on delete cascade,
  sensor_id integer not null,
  wake integer,
  moisture integer,
  battery_percent integer,
  rssi integer,
  age_ms integer,
  received_at timestamptz not null default now()
);

alter table public.msv2_sensor_history enable row level security;

create index if not exists msv2_sensor_history_receiver_sensor_time_idx
  on public.msv2_sensor_history(receiver_id, sensor_id, received_at desc);

create or replace function public.msv2_store_sensor_reading(
  p_receiver_id text,
  p_sensor_id integer,
  p_wake integer,
  p_moisture integer,
  p_battery_percent integer,
  p_rssi integer,
  p_age_ms integer
)
returns void
language plpgsql
security definer
set search_path = public
as $$
begin
  insert into public.msv2_sensor_history (
    receiver_id, sensor_id, wake, moisture, battery_percent, rssi, age_ms
  )
  values (
    p_receiver_id, p_sensor_id, p_wake, p_moisture, p_battery_percent, p_rssi, p_age_ms
  );
end;
$$;

create or replace function public.msv2_read_sensor_history(
  p_receiver_id text,
  p_sensor_id integer,
  p_since timestamptz default now() - interval '7 days',
  p_limit integer default 5000
)
returns table (
  sensor_id integer,
  wake integer,
  moisture integer,
  battery_percent integer,
  rssi integer,
  age_ms integer,
  received_at timestamptz
)
language sql
security definer
set search_path = public
as $$
  select h.sensor_id, h.wake, h.moisture, h.battery_percent, h.rssi, h.age_ms, h.received_at
  from public.msv2_sensor_history h
  where h.receiver_id = p_receiver_id
    and h.sensor_id = p_sensor_id
    and h.received_at >= p_since
  order by h.received_at asc
  limit p_limit;
$$;

-- Postgres grants EXECUTE to PUBLIC by default at function creation (see 012's note) - revoke
-- it explicitly here rather than relying on a follow-up hardening pass to close the gap.
revoke execute on function public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer) from public;
revoke execute on function public.msv2_read_sensor_history(text, integer, timestamptz, integer) from public;

grant execute on function public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer) to service_role;
grant execute on function public.msv2_read_sensor_history(text, integer, timestamptz, integer) to service_role;
