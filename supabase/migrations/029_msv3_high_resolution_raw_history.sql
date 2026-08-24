-- Preserve the MSV3 FDC2214's full wire resolution and safely version receiver-derived
-- moisture scales. Firmware v2 changes MSV3 from `(25M - raw) / 1000` to the much finer
-- `(24M - raw) / 100`; a 20M -> 19M dry/wet swing therefore spans 10,000 derived codes
-- instead of 1,000. The raw 28-bit FDC count is stored unchanged alongside that value.
--
-- Scale transitions must not contaminate either calibration or presentation history.
-- Existing rows are scale 1. The first scale-2 insert under any receiver/sensor pair
-- automatically advances its calibration reset epoch, and both history readers return
-- only the latest scale while all older rows remain preserved in the table.

alter table public.msv2_sensor_history
  add column if not exists moisture_scale integer not null default 1,
  add column if not exists raw_moisture_khz integer,
  add column if not exists raw_fdc_counts bigint;

drop function if exists public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer);

create or replace function public.msv2_store_sensor_reading(
  p_receiver_id text,
  p_sensor_id integer,
  p_wake integer,
  p_moisture integer,
  p_battery_percent integer,
  p_rssi integer,
  p_age_ms integer,
  p_display_moisture integer default null,
  p_moisture_scale integer default 1,
  p_raw_moisture_khz integer default null,
  p_raw_fdc_counts bigint default null
)
returns void
language plpgsql
security definer
set search_path = public
as $$
declare
  v_scale integer := greatest(1, coalesce(p_moisture_scale, 1));
  v_previous_scale integer;
begin
  select h.moisture_scale
    into v_previous_scale
  from public.msv2_sensor_history h
  where h.receiver_id = p_receiver_id
    and h.sensor_id = p_sensor_id
  order by h.id desc
  limit 1;

  if v_previous_scale is not null and v_previous_scale <> v_scale then
    insert into public.msv2_sensor_calibration_reset (
      receiver_id, sensor_id, reset_at, anchor_dry_bound, anchor_wet_bound
    )
    values (p_receiver_id, p_sensor_id, now(), null, null)
    on conflict (receiver_id, sensor_id) do update
      set reset_at = excluded.reset_at,
          anchor_dry_bound = null,
          anchor_wet_bound = null;
  end if;

  insert into public.msv2_sensor_history (
    receiver_id, sensor_id, wake, moisture, display_moisture, moisture_scale,
    raw_moisture_khz, raw_fdc_counts, battery_percent, rssi, age_ms
  )
  values (
    p_receiver_id, p_sensor_id, p_wake, p_moisture, p_display_moisture, v_scale,
    p_raw_moisture_khz, p_raw_fdc_counts, p_battery_percent, p_rssi, p_age_ms
  );
end;
$$;

revoke execute on function public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer, integer, integer, bigint) from public, anon, authenticated;
grant execute on function public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer, integer, integer, bigint) to service_role;

-- Receiver-token history reader: retain 014's ascending cursor, add the scale identifier,
-- and hide incompatible older scales from the plotted series.
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
  moisture_scale integer,
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
         h.moisture_scale, h.battery_percent, h.rssi, h.age_ms, h.received_at
  from public.msv2_sensor_history h
  where h.receiver_id = p_receiver_id
    and h.sensor_id = p_sensor_id
    and h.received_at >= p_since
    and h.id > p_after_id
    and h.moisture_scale = coalesce((
      select latest.moisture_scale
      from public.msv2_sensor_history latest
      where latest.receiver_id = p_receiver_id
        and latest.sensor_id = p_sensor_id
      order by latest.id desc
      limit 1
    ), 1)
  order by h.id asc
  limit p_limit;
$$;

revoke execute on function public.msv2_read_sensor_history(text, integer, timestamptz, bigint, integer) from public, anon, authenticated;
grant execute on function public.msv2_read_sensor_history(text, integer, timestamptz, bigint, integer) to service_role;

-- Household-share reader: retain 018's newest-first cursor and 020's service-role-only
-- lockdown while applying the same latest-scale filter.
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
  moisture_scale integer,
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
           h.moisture_scale, h.battery_percent, h.rssi, h.age_ms, h.received_at
    from public.msv2_sensor_history h
    where h.receiver_id = p_receiver_id
      and h.sensor_id = p_sensor_id
      and h.received_at >= p_since
      and (p_before_id = 0 or h.id < p_before_id)
      and h.moisture_scale = coalesce((
        select latest.moisture_scale
        from public.msv2_sensor_history latest
        where latest.receiver_id = p_receiver_id
          and latest.sensor_id = p_sensor_id
        order by latest.id desc
        limit 1
      ), 1)
    order by h.id desc
    limit p_limit;
end;
$$;

revoke execute on function public.msv2_read_share_sensor_history(text, text, integer, timestamptz, bigint, integer) from public, anon, authenticated;
grant execute on function public.msv2_read_share_sensor_history(text, text, integer, timestamptz, bigint, integer) to service_role;
