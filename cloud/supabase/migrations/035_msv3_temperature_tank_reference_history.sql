-- MSV3 software-only board (die) temperature + bare-tank FDC reference (2026-08-25). Two
-- independent raw observables from arduino/MSV3_Moisture_BLE_Sleep/MSV3_Moisture_BLE_Sleep.ino's
-- new 23-byte packet extension (see that file and Treebeard_ESP32_Display_Runtime.ino's
-- MSV3_DIAGNOSTIC_PACKET_LEN). Nullable throughout: absent for every existing sensor and for any
-- MSV3 probe still on pre-diagnostics firmware. This migration intentionally does NOT add or
-- enable any temperature-compensation/ratio formula - see
-- docs/MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md for the reserved formula and why
-- it stays unused for now.

alter table public.msv2_sensor_history
  add column if not exists board_temp_quarter_c smallint,
  add column if not exists bare_tank_fdc_counts bigint,
  add column if not exists tank_phase smallint;

-- msv2_store_sensor_reading gains three new trailing parameters, so its signature changes -
-- must drop the exact prior (034-unchanged-from-032) signature before recreating.
drop function if exists public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer, integer, integer, bigint, text, timestamptz, integer, boolean);

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
  p_raw_charging boolean default null,
  p_board_temp_quarter_c smallint default null,
  p_bare_tank_fdc_counts bigint default null,
  p_tank_phase smallint default null
)
returns void
language plpgsql
security definer
set search_path = public
as $$
declare
  v_scale integer := greatest(1, coalesce(p_moisture_scale, 1));
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

  perform public.msv2_apply_scale_transition_reset(p_receiver_id, p_sensor_id, v_scale, v_received_at);

  insert into public.msv2_sensor_history (
    receiver_id, sensor_id, wake, moisture, display_moisture, moisture_scale,
    raw_moisture_khz, raw_fdc_counts, battery_percent, rssi, age_ms,
    observation_id, received_at, raw_battery_mv, raw_charging,
    board_temp_quarter_c, bare_tank_fdc_counts, tank_phase
  )
  values (
    p_receiver_id, p_sensor_id, p_wake, p_moisture, p_display_moisture, v_scale,
    p_raw_moisture_khz, p_raw_fdc_counts, p_battery_percent, p_rssi, p_age_ms,
    p_observation_id, v_received_at, p_raw_battery_mv, p_raw_charging,
    p_board_temp_quarter_c, p_bare_tank_fdc_counts, p_tank_phase
  )
  on conflict do nothing;
end;
$$;

revoke execute on function public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer, integer, integer, bigint, text, timestamptz, integer, boolean, smallint, bigint, smallint) from public, anon, authenticated;
grant execute on function public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer, integer, integer, bigint, text, timestamptz, integer, boolean, smallint, bigint, smallint) to service_role;

-- msv2_store_sensor_reading_batch's own signature (text, jsonb) is unchanged - only its body
-- (three more fields extracted from each JSON item and forwarded) - so create-or-replace alone
-- is correct here, same as every prior migration that only touched this function's body.
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
      nullif(v_item->>'rawCharging', '')::boolean,
      nullif(v_item->>'boardTempQuarterC', '')::smallint,
      nullif(v_item->>'bareTankFdcCounts', '')::bigint,
      nullif(v_item->>'tankPhase', '')::smallint
    );
    v_accepted := v_accepted + 1;
  end loop;

  return v_accepted;
end;
$$;

revoke execute on function public.msv2_store_sensor_reading_batch(text, jsonb) from public, anon, authenticated;
grant execute on function public.msv2_store_sensor_reading_batch(text, jsonb) to service_role;

-- Both history readers' argument lists are unchanged, but their RETURNS TABLE column list
-- grows - Postgres does not allow changing a function's return shape via CREATE OR REPLACE, so
-- both must be dropped first even though callers pass the same arguments as before.
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
  board_temp_quarter_c smallint,
  bare_tank_fdc_counts bigint,
  tank_phase smallint,
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
         h.board_temp_quarter_c, h.bare_tank_fdc_counts, h.tank_phase,
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
  board_temp_quarter_c smallint,
  bare_tank_fdc_counts bigint,
  tank_phase smallint,
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
           h.board_temp_quarter_c, h.bare_tank_fdc_counts, h.tank_phase,
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
