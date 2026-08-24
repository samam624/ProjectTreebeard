-- Preserve raw battery voltage/charging-state per history row, alongside the existing raw
-- moisture columns (029). Previously only the receiver's own already-smoothed
-- `battery_percent` was ever stored - there was no way to recompute or re-tune battery
-- presentation smoothing after the fact, unlike moisture (whose `moisture` column has always
-- been the unsmoothed measurement). Nullable/optional throughout: older receivers, and any
-- probe still on the pre-rework legacy packet format (no raw fields at all - see hasRaw in
-- MSV2_ESP32_Display_Runtime.ino), simply omit these and callers fall back to
-- `battery_percent` as before.

alter table public.msv2_sensor_history
  add column if not exists raw_battery_mv integer,
  add column if not exists raw_charging boolean;

drop function if exists public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer, integer, integer, bigint, text, timestamptz);

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
  p_raw_fdc_counts bigint default null,
  p_observation_id text default null,
  p_received_at timestamptz default null,
  p_raw_battery_mv integer default null,
  p_raw_charging boolean default null
)
returns void
language plpgsql
security definer
set search_path = public
as $$
declare
  v_scale integer := greatest(1, coalesce(p_moisture_scale, 1));
  v_previous_scale integer;
  v_received_at timestamptz := coalesce(p_received_at, now());
begin
  -- A timed-out receiver retries the exact same queue prefix. Return before calibration
  -- side effects as well as before the insert so retries are fully idempotent.
  if p_observation_id is not null and exists (
    select 1
    from public.msv2_sensor_history h
    where h.receiver_id = p_receiver_id
      and h.observation_id = p_observation_id
  ) then
    return;
  end if;

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
    values (p_receiver_id, p_sensor_id, v_received_at, null, null)
    on conflict (receiver_id, sensor_id) do update
      set reset_at = excluded.reset_at,
          anchor_dry_bound = null,
          anchor_wet_bound = null;
  end if;

  insert into public.msv2_sensor_history (
    receiver_id, sensor_id, wake, moisture, display_moisture, moisture_scale,
    raw_moisture_khz, raw_fdc_counts, battery_percent, rssi, age_ms,
    observation_id, received_at, raw_battery_mv, raw_charging
  )
  values (
    p_receiver_id, p_sensor_id, p_wake, p_moisture, p_display_moisture, v_scale,
    p_raw_moisture_khz, p_raw_fdc_counts, p_battery_percent, p_rssi, p_age_ms,
    p_observation_id, v_received_at, p_raw_battery_mv, p_raw_charging
  )
  on conflict do nothing;
end;
$$;

revoke execute on function public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer, integer, integer, bigint, text, timestamptz, integer, boolean) from public, anon, authenticated;
grant execute on function public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer, integer, integer, bigint, text, timestamptz, integer, boolean) to service_role;

create or replace function public.msv2_store_sensor_reading_batch(
  p_receiver_id text,
  p_readings jsonb
)
returns integer
language plpgsql
security definer
set search_path = public
as $$
declare
  v_item jsonb;
  v_accepted integer := 0;
begin
  if jsonb_typeof(p_readings) <> 'array' then
    raise exception 'p_readings must be a JSON array';
  end if;
  if jsonb_array_length(p_readings) > 128 then
    raise exception 'history batch exceeds 128 rows';
  end if;

  for v_item in select value from jsonb_array_elements(p_readings)
  loop
    perform public.msv2_store_sensor_reading(
      p_receiver_id,
      (v_item->>'sensorId')::integer,
      nullif(v_item->>'wake', '')::integer,
      nullif(v_item->>'moisture', '')::integer,
      nullif(v_item->>'batteryPercent', '')::integer,
      nullif(v_item->>'rssi', '')::integer,
      nullif(v_item->>'ageMs', '')::integer,
      nullif(v_item->>'displayMoisture', '')::integer,
      coalesce(nullif(v_item->>'moistureScale', '')::integer, 1),
      nullif(v_item->>'rawMoistureKHz', '')::integer,
      nullif(v_item->>'rawFdcCounts', '')::bigint,
      nullif(v_item->>'observationId', ''),
      nullif(v_item->>'receivedAt', '')::timestamptz,
      nullif(v_item->>'rawBatteryMv', '')::integer,
      nullif(v_item->>'rawCharging', '')::boolean
    );
    v_accepted := v_accepted + 1;
  end loop;

  return v_accepted;
end;
$$;

revoke execute on function public.msv2_store_sensor_reading_batch(text, jsonb) from public, anon, authenticated;
grant execute on function public.msv2_store_sensor_reading_batch(text, jsonb) to service_role;

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
  moisture_scale integer,
  battery_percent integer,
  raw_battery_mv integer,
  raw_charging boolean,
  rssi integer,
  age_ms integer,
  received_at timestamptz
)
language sql
security definer
set search_path = public
as $$
  select h.id, h.sensor_id, h.wake, h.moisture, h.display_moisture,
         h.moisture_scale, h.battery_percent, h.raw_battery_mv, h.raw_charging,
         h.rssi, h.age_ms, h.received_at
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
  moisture_scale integer,
  battery_percent integer,
  raw_battery_mv integer,
  raw_charging boolean,
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
           h.moisture_scale, h.battery_percent, h.raw_battery_mv, h.raw_charging,
           h.rssi, h.age_ms, h.received_at
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
