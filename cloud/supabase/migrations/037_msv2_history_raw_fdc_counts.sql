-- Expose raw_fdc_counts/raw_moisture_khz (added to msv2_sensor_history by migration 029) through
-- the history-reader RPCs so they can actually be charted. This has been a gap since 029 itself -
-- that migration added the columns to the table and to the insert RPCs, but never added them to
-- msv2_read_sensor_history/msv2_read_share_sensor_history's own RETURNS TABLE, and the later
-- migration 036 msv2_read_share_sensor_history_sampled inherited the same omission. The raw value
-- has been visible in the live/latest snapshot (snapshotJson()'s plants[].raw.fdcCounts) and in
-- Supabase's underlying table the whole time, just never reachable through any History-tab code
-- path. Both receiver-snapshot/index.ts (calls msv2_read_sensor_history) and
-- plant-dashboard/index.ts (calls msv2_read_share_sensor_history_sampled) forward RPC rows through
-- to their JSON response generically (no explicit field allowlist), so no edge-function code
-- change is needed beyond this migration.

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
           h.raw_fdc_counts, h.raw_moisture_khz,
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

drop function if exists public.msv2_read_share_sensor_history_sampled(text, text, integer, timestamptz, bigint, bigint, integer, integer, integer);

create or replace function public.msv2_read_share_sensor_history_sampled(
  p_share_token text,
  p_receiver_id text,
  p_sensor_id integer,
  p_since timestamptz,
  p_at_or_before_id bigint,
  p_before_id bigint,
  p_moisture_scale integer,
  p_sample_every integer,
  p_limit integer
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

  -- Rank against the same frozen newest-id/scale snapshot on every internal page. Keeping
  -- p_before_id outside this CTE means the modulo phase does not reset at page boundaries,
  -- so the result really is one stable row out of every N across the complete time window.
  return query
    with ranked as (
      select h.id, h.sensor_id, h.wake, h.moisture, h.display_moisture,
             h.moisture_scale, h.battery_percent, h.raw_battery_mv, h.raw_charging,
             h.raw_fdc_counts, h.raw_moisture_khz,
             h.board_temp_quarter_c, h.bare_tank_fdc_counts, h.tank_phase,
             h.rssi, h.age_ms, h.received_at,
             row_number() over (order by h.id desc) - 1 as sample_index
      from public.msv2_sensor_history h
      where h.receiver_id = p_receiver_id
        and h.sensor_id = p_sensor_id
        and h.received_at >= p_since
        and h.id <= p_at_or_before_id
        and h.moisture_scale = p_moisture_scale
    )
    select r.id, r.sensor_id, r.wake, r.moisture, r.display_moisture,
           r.moisture_scale, r.battery_percent, r.raw_battery_mv, r.raw_charging,
           r.raw_fdc_counts, r.raw_moisture_khz,
           r.board_temp_quarter_c, r.bare_tank_fdc_counts, r.tank_phase,
           r.rssi, r.age_ms, r.received_at
    from ranked r
    where mod(r.sample_index, v_sample_every::bigint) = 0
      and (p_before_id = 0 or r.id < p_before_id)
    order by r.id desc
    limit v_limit;
end;
$$;

revoke execute on function public.msv2_read_share_sensor_history_sampled(text, text, integer, timestamptz, bigint, bigint, integer, integer, integer) from public, anon, authenticated;
grant execute on function public.msv2_read_share_sensor_history_sampled(text, text, integer, timestamptz, bigint, bigint, integer, integer, integer) to service_role;
