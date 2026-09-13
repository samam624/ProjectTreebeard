-- Store the DisplayCenter's MSV3 temperature-compensation shadow channel alongside (never in
-- place of) raw connected FDC, bare-tank FDC, board temperature, and the original calibrated
-- moisture value. The signal is ln(Cconnected/Cbare) in parts per million: a dimensionless,
-- reversible representation with enough resolution for the small remaining moisture trend.

alter table public.msv2_sensor_history
  add column if not exists temperature_compensated_log_ratio_ppm integer,
  add column if not exists temperature_compensation_model_version smallint;

-- Two new trailing arguments change the store function signature.
drop function if exists public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer, integer, integer, bigint, text, timestamptz, integer, boolean, smallint, bigint, smallint);

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
  p_tank_phase smallint default null,
  p_temperature_compensated_log_ratio_ppm integer default null,
  p_temperature_compensation_model_version smallint default null
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
    board_temp_quarter_c, bare_tank_fdc_counts, tank_phase,
    temperature_compensated_log_ratio_ppm, temperature_compensation_model_version
  )
  values (
    p_receiver_id, p_sensor_id, p_wake, p_moisture, p_display_moisture, v_scale,
    p_raw_moisture_khz, p_raw_fdc_counts, p_battery_percent, p_rssi, p_age_ms,
    p_observation_id, v_received_at, p_raw_battery_mv, p_raw_charging,
    p_board_temp_quarter_c, p_bare_tank_fdc_counts, p_tank_phase,
    p_temperature_compensated_log_ratio_ppm, p_temperature_compensation_model_version
  )
  on conflict do nothing;
end;
$$;

revoke execute on function public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer, integer, integer, bigint, text, timestamptz, integer, boolean, smallint, bigint, smallint, integer, smallint) from public, anon, authenticated;
grant execute on function public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer, integer, integer, bigint, text, timestamptz, integer, boolean, smallint, bigint, smallint, integer, smallint) to service_role;

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
      nullif(v_item->>'tankPhase', '')::smallint,
      nullif(v_item->>'temperatureCompensatedLogRatioPpm', '')::integer,
      nullif(v_item->>'temperatureCompensationModelVersion', '')::smallint
    );
    v_accepted := v_accepted + 1;
  end loop;

  return v_accepted;
end;
$$;

revoke execute on function public.msv2_store_sensor_reading_batch(text, jsonb) from public, anon, authenticated;
grant execute on function public.msv2_store_sensor_reading_batch(text, jsonb) to service_role;

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
  raw_fdc_counts bigint,
  raw_moisture_khz integer,
  board_temp_quarter_c smallint,
  bare_tank_fdc_counts bigint,
  tank_phase smallint,
  temperature_compensated_log_ratio_ppm integer,
  temperature_compensation_model_version smallint,
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
         h.raw_fdc_counts, h.raw_moisture_khz,
         h.board_temp_quarter_c, h.bare_tank_fdc_counts, h.tank_phase,
         h.temperature_compensated_log_ratio_ppm, h.temperature_compensation_model_version,
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
  raw_fdc_counts bigint,
  raw_moisture_khz integer,
  board_temp_quarter_c smallint,
  bare_tank_fdc_counts bigint,
  tank_phase smallint,
  temperature_compensated_log_ratio_ppm integer,
  temperature_compensation_model_version smallint,
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
  select t.household_id into v_household_id
  from public.msv2_household_share_tokens t
  where t.token_hash = public.msv2_hash_secret(p_share_token)
    and t.revoked_at is null;

  if v_household_id is null then raise exception 'invalid share token'; end if;
  if not exists (
    select 1 from public.msv2_receiver_claims c
    where c.household_id = v_household_id and c.receiver_id = p_receiver_id
  ) then raise exception 'receiver is not linked to this household'; end if;

  return query
    select h.id, h.sensor_id, h.wake, h.moisture, h.display_moisture,
           h.moisture_scale, h.battery_percent, h.raw_battery_mv, h.raw_charging,
           h.raw_fdc_counts, h.raw_moisture_khz,
           h.board_temp_quarter_c, h.bare_tank_fdc_counts, h.tank_phase,
           h.temperature_compensated_log_ratio_ppm, h.temperature_compensation_model_version,
           h.rssi, h.age_ms, h.received_at
    from public.msv2_sensor_history h
    where h.receiver_id = p_receiver_id
      and h.sensor_id = p_sensor_id
      and h.received_at >= p_since
      and (p_before_id = 0 or h.id < p_before_id)
      and h.moisture_scale = coalesce((
        select latest.moisture_scale
        from public.msv2_sensor_history latest
        where latest.receiver_id = p_receiver_id and latest.sensor_id = p_sensor_id
        order by latest.id desc limit 1
      ), 1)
    order by h.id desc
    limit p_limit;
end;
$$;

revoke execute on function public.msv2_read_share_sensor_history(text, text, integer, timestamptz, bigint, integer) from public, anon, authenticated;
grant execute on function public.msv2_read_share_sensor_history(text, text, integer, timestamptz, bigint, integer) to service_role;

-- Migration 038 added the final p_until argument; preserve that current signature and range
-- behavior while extending only the returned row shape.
drop function if exists public.msv2_read_share_sensor_history_sampled(text, text, integer, timestamptz, bigint, bigint, integer, integer, integer, timestamptz);

create or replace function public.msv2_read_share_sensor_history_sampled(
  p_share_token text,
  p_receiver_id text,
  p_sensor_id integer,
  p_since timestamptz,
  p_at_or_before_id bigint,
  p_before_id bigint,
  p_moisture_scale integer,
  p_sample_every integer,
  p_limit integer,
  p_until timestamptz default null
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
  raw_fdc_counts bigint,
  raw_moisture_khz integer,
  board_temp_quarter_c smallint,
  bare_tank_fdc_counts bigint,
  tank_phase smallint,
  temperature_compensated_log_ratio_ppm integer,
  temperature_compensation_model_version smallint,
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
  v_sample_every integer := least(greatest(coalesce(p_sample_every, 1), 1), 1000000);
  v_limit integer := least(greatest(coalesce(p_limit, 500), 1), 2000);
begin
  select t.household_id into v_household_id
  from public.msv2_household_share_tokens t
  where t.token_hash = public.msv2_hash_secret(p_share_token)
    and t.revoked_at is null;

  if v_household_id is null then raise exception 'invalid share token'; end if;
  if not exists (
    select 1 from public.msv2_receiver_claims c
    where c.household_id = v_household_id and c.receiver_id = p_receiver_id
  ) then raise exception 'receiver is not linked to this household'; end if;

  return query
    with ranked as (
      select h.id, h.sensor_id, h.wake, h.moisture, h.display_moisture,
             h.moisture_scale, h.battery_percent, h.raw_battery_mv, h.raw_charging,
             h.raw_fdc_counts, h.raw_moisture_khz,
             h.board_temp_quarter_c, h.bare_tank_fdc_counts, h.tank_phase,
             h.temperature_compensated_log_ratio_ppm, h.temperature_compensation_model_version,
             h.rssi, h.age_ms, h.received_at,
             row_number() over (order by h.id desc) - 1 as sample_index
      from public.msv2_sensor_history h
      where h.receiver_id = p_receiver_id
        and h.sensor_id = p_sensor_id
        and h.received_at >= p_since
        and (p_until is null or h.received_at <= p_until)
        and h.id <= p_at_or_before_id
        and h.moisture_scale = p_moisture_scale
    )
    select r.id, r.sensor_id, r.wake, r.moisture, r.display_moisture,
           r.moisture_scale, r.battery_percent, r.raw_battery_mv, r.raw_charging,
           r.raw_fdc_counts, r.raw_moisture_khz,
           r.board_temp_quarter_c, r.bare_tank_fdc_counts, r.tank_phase,
           r.temperature_compensated_log_ratio_ppm, r.temperature_compensation_model_version,
           r.rssi, r.age_ms, r.received_at
    from ranked r
    where mod(r.sample_index, v_sample_every::bigint) = 0
      and (p_before_id = 0 or r.id < p_before_id)
    order by r.id desc
    limit v_limit;
end;
$$;

revoke execute on function public.msv2_read_share_sensor_history_sampled(text, text, integer, timestamptz, bigint, bigint, integer, integer, integer, timestamptz) from public, anon, authenticated;
grant execute on function public.msv2_read_share_sensor_history_sampled(text, text, integer, timestamptz, bigint, bigint, integer, integer, integer, timestamptz) to service_role;
