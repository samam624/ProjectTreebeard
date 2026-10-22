-- 2026-10-21: the "water now" region stops following the level each watering settles at
-- (supabase/functions/receiver-snapshot/region.ts, rule 7; cloud/docs/WATER_NOW_RATCHET_2026-10-21.md).
--
-- With the line a fixed drop below each settled level, a user who waters at the line waters a pot
-- that has not dried, it settles wetter, the line follows it up, and the next watering comes
-- sooner. Measured on probe 7: the line rose from 42,623 to 44,903 in ten days and the watering
-- interval fell from 3.3 to 2.1 days. Once the drop is taught the region is now fixed in raw
-- readings: a lower settled level may still lower it, nothing raises it.
--
-- The region is recomputed from at most 60 days of hours, so a fixed region (and every lowering
-- of it) is held here; otherwise it would be re-derived from later waterings once the ones that
-- fixed it left the window. Additive: nothing existing changes meaning.

alter table public.msv2_sensor_calibration_reset
  -- Raw readings: water now at or below region_top, overdue below region_bottom, 100 % at
  -- region_full. Set together, by msv2_set_sensor_region_line_if_lower only.
  add column if not exists region_top numeric null,
  add column if not exists region_bottom numeric null,
  add column if not exists region_full numeric null,
  -- The watering (hours since 1970-01-01 UTC) that fixed or last lowered it. region.ts applies
  -- only later waterings to a stored region; without this every pull would lower it again.
  add column if not exists region_line_hour bigint null,
  add column if not exists region_line_set_at timestamptz null;

-- A calibration reset ("a different plant, pot or soil now", or a new reading scale) clears the
-- fixed region with the learned drop. It is the one way to move a region up.
create or replace function public.msv2_clear_region_anchor_on_reset()
returns trigger
language plpgsql
as $$
begin
  if new.reset_at is distinct from old.reset_at then
    new.region_drop := null;
    new.region_mode := null;
    new.region_set_at := null;
    new.region_top := null;
    new.region_bottom := null;
    new.region_full := null;
    new.region_line_hour := null;
    new.region_line_set_at := null;
  end if;
  return new;
end;
$$;

-- Internal only (service_role): receiver-snapshot stores the region when it is first fixed and
-- whenever a watering lowers it. "where region_top is null or new top is lower" makes it
-- idempotent and race-safe, and means nothing can ever store a higher one.
create or replace function public.msv2_set_sensor_region_line_if_lower(
  p_receiver_id text,
  p_sensor_id integer,
  p_region_top numeric,
  p_region_bottom numeric,
  p_region_full numeric,
  p_region_line_hour bigint
)
returns void
language plpgsql
security definer
set search_path = public
as $$
begin
  if p_region_top is null or p_region_bottom is null or p_region_full is null or p_region_line_hour is null
      or not (p_region_bottom < p_region_top and p_region_top < p_region_full) then
    return;
  end if;
  insert into public.msv2_sensor_calibration_reset (receiver_id, sensor_id, region_top, region_bottom, region_full, region_line_hour, region_line_set_at)
  values (p_receiver_id, p_sensor_id, p_region_top, p_region_bottom, p_region_full, p_region_line_hour, now())
  on conflict (receiver_id, sensor_id) do update
    set region_top = excluded.region_top,
        region_bottom = excluded.region_bottom,
        region_full = excluded.region_full,
        region_line_hour = excluded.region_line_hour,
        region_line_set_at = excluded.region_line_set_at
    where msv2_sensor_calibration_reset.region_top is null
       or excluded.region_top < msv2_sensor_calibration_reset.region_top;
end;
$$;

revoke execute on function public.msv2_set_sensor_region_line_if_lower(text, integer, numeric, numeric, numeric, bigint) from public, anon, authenticated;
grant execute on function public.msv2_set_sensor_region_line_if_lower(text, integer, numeric, numeric, numeric, bigint) to service_role;

-- As in 041, with the stored region added to each sensor's row.
create or replace function public.msv2_read_sensor_hourly_moisture(
  p_receiver_id text,
  p_lookback_days integer default 60
)
returns jsonb
language plpgsql
security definer
set search_path = public
as $$
declare
  v_result jsonb;
  v_lookback timestamptz := now() - (p_lookback_days || ' days')::interval;
  v_last_hour bigint;
begin
  select max(c.hour) into v_last_hour
  from public.msv2_sensor_hourly_moisture c
  where c.receiver_id = p_receiver_id;

  perform public.msv2_refresh_sensor_hourly_moisture(
    p_receiver_id,
    greatest(v_lookback, least(now() - interval '48 hours', coalesce(to_timestamp(v_last_hour * 3600.0), v_lookback)))
  );

  with resets as (
    select sensor_id, reset_at, region_drop, region_mode, region_base, region_top, region_bottom, region_full, region_line_hour
    from public.msv2_sensor_calibration_reset
    where receiver_id = p_receiver_id
  ),
  latest_scale as (
    select i.sensor_id, l.moisture_scale
    from generate_series(1, 32) as i(sensor_id)
    cross join lateral (
      select h.moisture_scale
      from public.msv2_sensor_history h
      where h.receiver_id = p_receiver_id
        and h.sensor_id = i.sensor_id
        and h.received_at >= v_lookback
      order by h.received_at desc
      limit 1
    ) l
  )
  select jsonb_agg(
    jsonb_build_object(
      'sensorId', s.sensor_id,
      'scale', s.moisture_scale,
      'regionDrop', r.region_drop,
      'regionMode', r.region_mode,
      'regionBase', r.region_base,
      'regionTop', r.region_top,
      'regionBottom', r.region_bottom,
      'regionFull', r.region_full,
      'regionLineHour', r.region_line_hour,
      'hours', coalesce((
        select jsonb_agg(jsonb_build_array(c.hour, round(c.moisture)) order by c.hour)
        from public.msv2_sensor_hourly_moisture c
        where c.receiver_id = p_receiver_id
          and c.sensor_id = s.sensor_id
          and c.moisture_scale = s.moisture_scale
          -- ceil: the hour a reset falls in still holds readings from before it
          and c.hour >= ceil(extract(epoch from greatest(v_lookback, coalesce(r.reset_at, '-infinity'::timestamptz))) / 3600)::bigint
      ), '[]'::jsonb)
    )
    order by s.sensor_id
  )
  into v_result
  from latest_scale s
  left join resets r on r.sensor_id = s.sensor_id;

  return coalesce(v_result, '[]'::jsonb);
end;
$$;

revoke execute on function public.msv2_read_sensor_hourly_moisture(text, integer) from public, anon, authenticated;
grant execute on function public.msv2_read_sensor_hourly_moisture(text, integer) to service_role;
