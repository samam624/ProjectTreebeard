-- Add an optional upper-bound timestamp (p_until) to the share-history density/sampled reader
-- pair from migration 036, so the History tab can query an arbitrary closed date range (e.g.
-- 2026-10-29 to 2026-10-31) instead of only "since X, through whatever's newest right now."
-- p_until defaults to null (no upper bound) so every existing caller's behavior is unchanged -
-- this is purely additive. Adding a new parameter changes the function's argument-type
-- signature even with a default, so both must be dropped before recreating, same as every
-- prior migration that changed one of these two functions' shape.

drop function if exists public.msv2_read_share_sensor_history_density(text, text, integer, timestamptz);

create or replace function public.msv2_read_share_sensor_history_density(
  p_share_token text,
  p_receiver_id text,
  p_sensor_id integer,
  p_since timestamptz,
  p_until timestamptz default null
)
returns table (
  total_rows bigint,
  newest_id bigint,
  moisture_scale integer
)
language plpgsql
security definer
set search_path = public, extensions
as $$
declare
  v_household_id uuid;
  v_total_rows bigint := 0;
  v_newest_id bigint := 0;
  v_moisture_scale integer;
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

  -- Scale is still taken from the latest row overall (not bounded by p_until) - a custom
  -- historical window should still resolve against whichever calibration epoch the receiver
  -- is currently on, same as every other history read in this project, not against whatever
  -- scale happened to be active at the end of the requested window.
  select h.moisture_scale
    into v_moisture_scale
  from public.msv2_sensor_history h
  where h.receiver_id = p_receiver_id
    and h.sensor_id = p_sensor_id
  order by h.id desc
  limit 1;

  if v_moisture_scale is not null then
    select count(*), coalesce(max(h.id), 0)
      into v_total_rows, v_newest_id
    from public.msv2_sensor_history h
    where h.receiver_id = p_receiver_id
      and h.sensor_id = p_sensor_id
      and h.received_at >= p_since
      and (p_until is null or h.received_at <= p_until)
      and h.moisture_scale = v_moisture_scale;
  end if;

  return query select v_total_rows, v_newest_id, v_moisture_scale;
end;
$$;

revoke execute on function public.msv2_read_share_sensor_history_density(text, text, integer, timestamptz, timestamptz) from public, anon, authenticated;
grant execute on function public.msv2_read_share_sensor_history_density(text, text, integer, timestamptz, timestamptz) to service_role;

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
        and (p_until is null or h.received_at <= p_until)
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

revoke execute on function public.msv2_read_share_sensor_history_sampled(text, text, integer, timestamptz, bigint, bigint, integer, integer, integer, timestamptz) from public, anon, authenticated;
grant execute on function public.msv2_read_share_sensor_history_sampled(text, text, integer, timestamptz, bigint, bigint, integer, integer, integer, timestamptz) to service_role;
