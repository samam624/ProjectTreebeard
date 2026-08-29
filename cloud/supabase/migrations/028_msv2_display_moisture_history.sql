-- Separate presentation smoothing from measurement truth.
--
-- `moisture` intentionally remains the unsmoothed receiver-derived measurement used by
-- auto-calibration and diagnostics. `display_moisture` records exactly what the physical
-- receiver and web dashboards showed: a clean monotonic dry-down with watering rises.
-- Existing rows remain valid (display_moisture is null and clients fall back to moisture),
-- and the new store-RPC argument defaults to null so old receiver-snapshot deployments can
-- continue writing while this migration and the updated edge function roll out.

alter table public.msv2_sensor_history
  add column if not exists display_moisture integer;

drop function if exists public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer);

create or replace function public.msv2_store_sensor_reading(
  p_receiver_id text,
  p_sensor_id integer,
  p_wake integer,
  p_moisture integer,
  p_battery_percent integer,
  p_rssi integer,
  p_age_ms integer,
  p_display_moisture integer default null
)
returns void
language plpgsql
security definer
set search_path = public
as $$
begin
  insert into public.msv2_sensor_history (
    receiver_id, sensor_id, wake, moisture, display_moisture,
    battery_percent, rssi, age_ms
  )
  values (
    p_receiver_id, p_sensor_id, p_wake, p_moisture, p_display_moisture,
    p_battery_percent, p_rssi, p_age_ms
  );
end;
$$;

revoke execute on function public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer) from public, anon, authenticated;
grant execute on function public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer) to service_role;

-- Receiver-token history reader (014 cursor direction retained).
drop function if exists public.msv2_read_sensor_history(text, integer, timestamptz, bigint, integer);

create or replace function public.msv2_read_sensor_history(
  p_receiver_id text,
  p_sensor_id integer,
  p_since timestamptz default now() - interval '7 days',
  p_after_id bigint default 0,
  p_limit integer default 500
)
returns table (
  id bigint,
  sensor_id integer,
  wake integer,
  moisture integer,
  display_moisture integer,
  battery_percent integer,
  rssi integer,
  age_ms integer,
  received_at timestamptz
)
language sql
security definer
set search_path = public
as $$
  select h.id, h.sensor_id, h.wake, h.moisture, h.display_moisture,
         h.battery_percent, h.rssi, h.age_ms, h.received_at
  from public.msv2_sensor_history h
  where h.receiver_id = p_receiver_id
    and h.sensor_id = p_sensor_id
    and h.received_at >= p_since
    and h.id > p_after_id
  order by h.id asc
  limit p_limit;
$$;

revoke execute on function public.msv2_read_sensor_history(text, integer, timestamptz, bigint, integer) from public, anon, authenticated;
grant execute on function public.msv2_read_sensor_history(text, integer, timestamptz, bigint, integer) to service_role;

-- Household-share history reader (018 newest-first cursor and 020 RPC lockdown retained).
drop function if exists public.msv2_read_share_sensor_history(text, text, integer, timestamptz, bigint, integer);

create or replace function public.msv2_read_share_sensor_history(
  p_share_token text,
  p_receiver_id text,
  p_sensor_id integer,
  p_since timestamptz default now() - interval '7 days',
  p_before_id bigint default 0,
  p_limit integer default 500
)
returns table (
  id bigint,
  sensor_id integer,
  wake integer,
  moisture integer,
  display_moisture integer,
  battery_percent integer,
  rssi integer,
  age_ms integer,
  received_at timestamptz
)
language plpgsql
security definer
set search_path = public, extensions
as $$
declare
  v_household_id uuid;
begin
  select t.household_id
    into v_household_id
  from public.msv2_household_share_tokens t
  where t.token_hash = public.msv2_hash_secret(p_share_token)
    and t.revoked_at is null;

  if v_household_id is null then
    raise exception 'invalid share token';
  end if;

  if not exists (
    select 1
    from public.msv2_receiver_claims c
    where c.household_id = v_household_id
      and c.receiver_id = p_receiver_id
  ) then
    raise exception 'receiver is not linked to this household';
  end if;

  return query
    select h.id, h.sensor_id, h.wake, h.moisture, h.display_moisture,
           h.battery_percent, h.rssi, h.age_ms, h.received_at
    from public.msv2_sensor_history h
    where h.receiver_id = p_receiver_id
      and h.sensor_id = p_sensor_id
      and h.received_at >= p_since
      and (p_before_id = 0 or h.id < p_before_id)
    order by h.id desc
    limit p_limit;
end;
$$;

revoke execute on function public.msv2_read_share_sensor_history(text, text, integer, timestamptz, bigint, integer) from public, anon, authenticated;
grant execute on function public.msv2_read_share_sensor_history(text, text, integer, timestamptz, bigint, integer) to service_role;
