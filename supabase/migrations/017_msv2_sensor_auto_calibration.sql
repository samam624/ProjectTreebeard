-- Computes a self-calibrating moisture range per sensor from its own history, instead
-- of relying on manually-guessed dry/wet thresholds (which have drifted out of sync
-- with the sensing scale twice in one day already - see docs/MOISTURE_SENSOR_OSCILLATOR_REPORT.md
-- and the BLE sketch's moisture-inversion change). Uses the 5th/95th percentile over a
-- trailing window rather than raw min/max, so a single outlier reading can't redefine
-- the learned range, and a one-time probe removal/reinsertion transient dilutes away
-- within the window instead of being permanent. Also flags candidate "watering event"
-- rises (a fast, large jump in moisture) for the caller (receiver-snapshot) to cluster
-- into discrete events and derive a watering interval - that clustering/date-math is
-- done in TypeScript at the edge function, not here, since it's much more naturally
-- expressed as imperative code than SQL window-function chains.

create or replace function public.msv2_read_sensor_auto_calibration(
  p_receiver_id text,
  p_lookback_days integer default 21
)
returns jsonb
language plpgsql
security definer
set search_path = public
as $$
declare
  v_result jsonb;
begin
  with windowed as (
    select
      h.sensor_id,
      h.moisture,
      h.received_at,
      h.moisture - lag(h.moisture) over (partition by h.sensor_id order by h.received_at) as delta,
      h.received_at - lag(h.received_at) over (partition by h.sensor_id order by h.received_at) as gap
    from public.msv2_sensor_history h
    where h.receiver_id = p_receiver_id
      and h.received_at >= now() - (p_lookback_days || ' days')::interval
      and h.moisture is not null
  ),
  bounds as (
    select
      sensor_id,
      count(*) as sample_count,
      min(received_at) as oldest_reading_at,
      percentile_cont(0.05) within group (order by moisture) as dry_bound,
      percentile_cont(0.95) within group (order by moisture) as wet_bound
    from windowed
    group by sensor_id
  ),
  rises as (
    select
      w.sensor_id,
      w.received_at,
      w.delta
    from windowed w
    join bounds b on b.sensor_id = w.sensor_id
    where w.delta is not null
      and w.gap <= interval '3 hours'
      and (b.wet_bound - b.dry_bound) > 0
      and w.delta >= 0.15 * (b.wet_bound - b.dry_bound)
  )
  select jsonb_agg(
    jsonb_build_object(
      'sensorId', b.sensor_id,
      'sampleCount', b.sample_count,
      'oldestReadingAt', b.oldest_reading_at,
      'dryBound', round(b.dry_bound),
      'wetBound', round(b.wet_bound),
      'rises', coalesce((
        select jsonb_agg(jsonb_build_object('at', r.received_at, 'delta', round(r.delta)) order by r.received_at)
        from rises r
        where r.sensor_id = b.sensor_id
      ), '[]'::jsonb)
    )
  )
  into v_result
  from bounds b;

  return coalesce(v_result, '[]'::jsonb);
end;
$$;

revoke execute on function public.msv2_read_sensor_auto_calibration(text, integer) from public;
grant execute on function public.msv2_read_sensor_auto_calibration(text, integer) to service_role;
