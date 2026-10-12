-- 2026-10-11: the "water now" region (cloud/docs/MOISTURE_PERCENT_RANGE_AND_FERTILIZER_2026-10-11.md,
-- supabase/functions/receiver-snapshot/region.ts).
--
-- The learned dry/wet range (migrations 017-027) takes 0 % from where the last waterings
-- happened and 100 % from the pour's peak, so watering early moves 0 % up and one fertilized
-- watering stretches the range. The region takes 100 % from the level the pot SETTLES at after
-- each watering and puts the watering line a fixed drop below it. That drop is learned once, from
-- the first waterings after a calibration reset, and then held here so no later watering can
-- move it.
--
-- Additive: nothing existing is redefined. Receivers that do not ask for the region
-- (receiver-snapshot?autocal=1 without region=1) get exactly what they got before.

alter table public.msv2_sensor_calibration_reset
  -- The learned drop below the settled level at which this sensor's pot gets watered. In the
  -- units of region_mode: a share of (settled - base) for 'share', raw reading units for 'span'.
  add column if not exists region_drop numeric null,
  add column if not exists region_mode text null,
  add column if not exists region_set_at timestamptz null,
  -- Share mode only: the reading the water-dependent part scales about, for a probe whose
  -- in-air value is not known from its moisture scale. Set by hand. A property of the probe, so
  -- a calibration reset does not clear it.
  add column if not exists region_base numeric null;

alter table public.msv2_sensor_calibration_reset
  drop constraint if exists msv2_sensor_calibration_reset_region_mode_check;
alter table public.msv2_sensor_calibration_reset
  add constraint msv2_sensor_calibration_reset_region_mode_check
  check (region_mode is null or region_mode in ('share', 'span'));

-- A calibration reset means "a different plant, pot or soil now" (msv2_reset_sensor_calibration)
-- or "a different reading scale now" (msv2_apply_scale_transition_reset). Either way the learned
-- drop is as stale as the learned range, and the next waterings teach a new one. A trigger, so
-- neither of those functions has to be redefined to stay in step.
create or replace function public.msv2_clear_region_anchor_on_reset()
returns trigger
language plpgsql
as $$
begin
  if new.reset_at is distinct from old.reset_at then
    new.region_drop := null;
    new.region_mode := null;
    new.region_set_at := null;
  end if;
  return new;
end;
$$;

drop trigger if exists msv2_clear_region_anchor_on_reset on public.msv2_sensor_calibration_reset;
create trigger msv2_clear_region_anchor_on_reset
  before update on public.msv2_sensor_calibration_reset
  for each row execute function public.msv2_clear_region_anchor_on_reset();

-- Internal only (service_role): receiver-snapshot stores the drop the first time two waterings
-- have taught it. "where region_drop is null" makes it idempotent and race-safe, the same way
-- msv2_set_sensor_calibration_anchor_if_unset is: it can only ever fill an empty anchor.
create or replace function public.msv2_set_sensor_region_anchor_if_unset(
  p_receiver_id text,
  p_sensor_id integer,
  p_region_drop numeric,
  p_region_mode text
)
returns void
language plpgsql
security definer
set search_path = public
as $$
begin
  if p_region_drop is null or p_region_drop <= 0 or p_region_mode not in ('share', 'span') then
    return;
  end if;
  insert into public.msv2_sensor_calibration_reset (receiver_id, sensor_id, region_drop, region_mode, region_set_at)
  values (p_receiver_id, p_sensor_id, p_region_drop, p_region_mode, now())
  on conflict (receiver_id, sensor_id) do update
    set region_drop = excluded.region_drop,
        region_mode = excluded.region_mode,
        region_set_at = excluded.region_set_at
    where msv2_sensor_calibration_reset.region_drop is null;
end;
$$;

revoke execute on function public.msv2_set_sensor_region_anchor_if_unset(text, integer, numeric, text) from public, anon, authenticated;
grant execute on function public.msv2_set_sensor_region_anchor_if_unset(text, integer, numeric, text) to service_role;

-- One median per sensor per hour, kept in a table. Reading 60 days of raw rows on every pull
-- took 6 s warm and 18 s cold on the live database (measured 2026-10-11), against a receiver
-- that waits 8 s; a closed hour's median does not change, so only the trailing hours are
-- recomputed (18-120 ms for 48 h, per-sensor so the (receiver, sensor, time) index is used).
create table if not exists public.msv2_sensor_hourly_moisture (
  receiver_id text not null,
  sensor_id integer not null,
  moisture_scale integer not null,
  hour bigint not null,              -- hours since 1970-01-01 00:00 UTC
  moisture numeric not null,         -- median of that hour's readings
  sample_count integer not null,
  primary key (receiver_id, sensor_id, moisture_scale, hour)
);

alter table public.msv2_sensor_hourly_moisture enable row level security;

-- Recompute every hour from p_since on. Idempotent. Sensor IDs are probed one at a time (the
-- receiver accepts 1-16; 32 leaves room) because a query that leaves sensor_id out walks the
-- whole receiver's index.
create or replace function public.msv2_refresh_sensor_hourly_moisture(
  p_receiver_id text,
  p_since timestamptz
)
returns void
language sql
security definer
set search_path = public
as $$
  insert into public.msv2_sensor_hourly_moisture (receiver_id, sensor_id, moisture_scale, hour, moisture, sample_count)
  select p_receiver_id, i.sensor_id, x.moisture_scale, x.hour, x.moisture, x.sample_count
  from generate_series(1, 32) as i(sensor_id)
  cross join lateral (
    select
      h.moisture_scale,
      floor(extract(epoch from h.received_at) / 3600)::bigint as hour,
      percentile_cont(0.50) within group (order by h.moisture::numeric) as moisture,
      count(*)::integer as sample_count
    from public.msv2_sensor_history h
    where h.receiver_id = p_receiver_id
      and h.sensor_id = i.sensor_id
      and h.received_at >= date_trunc('hour', p_since)
      and h.moisture is not null
    group by h.moisture_scale, floor(extract(epoch from h.received_at) / 3600)::bigint
  ) x
  on conflict (receiver_id, sensor_id, moisture_scale, hour) do update
    set moisture = excluded.moisture,
        sample_count = excluded.sample_count;
$$;

revoke execute on function public.msv2_refresh_sensor_hourly_moisture(text, timestamptz) from public, anon, authenticated;
grant execute on function public.msv2_refresh_sensor_hourly_moisture(text, timestamptz) to service_role;

-- What region.ts reads: each sensor's hourly medians since the later of the lookback and its
-- calibration reset, on its current moisture scale only, with its stored anchor. Refreshes the
-- trailing hours first: back to the last cached hour when the receiver has been away, and never
-- less than 48 h, because the receiver's outbox can deliver an hour's rows long after the hour.
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
    select sensor_id, reset_at, region_drop, region_mode, region_base
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

-- Fill the table for every receiver on record, so the first pull after this migration reads a
-- warm table and is not the one that pays for 60 days.
select public.msv2_refresh_sensor_hourly_moisture(r.receiver_id, now() - interval '60 days')
from public.msv2_receivers r;
