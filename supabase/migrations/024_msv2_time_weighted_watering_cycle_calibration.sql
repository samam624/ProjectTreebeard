-- Make moisture auto-calibration describe the plant's most recent real watering cycle
-- instead of the raw distribution of snapshot rows.
--
-- The previous implementation calculated 5th/95th percentiles directly over
-- msv2_sensor_history. That table records one row for every uploaded receiver snapshot,
-- so a temporary high upload cadence could give one day thousands of times more weight
-- than another day. It also made "0%" mean the rolling 5th percentile, not "as dry as
-- the soil was when this plant was last watered."
--
-- This replacement:
--   1. collapses history into one robust median reading per five-minute time bucket;
--   2. uses those uniformly weighted buckets for fallback 5th/95th bounds;
--   3. identifies only large, nearby upward steps as candidate watering cycles; and
--   4. returns the pre-watering reading and the following three-hour wet peak with each
--      rise, allowing receiver-snapshot/deriveAutoCal() to use the latest cycle as the
--      actual dry/wet range.
--
-- The existing reset epoch still applies. Legacy anchor columns remain in the response
-- for rollout compatibility, but the edge function no longer uses the old rolling-floor
-- clamp once it has watering-cycle landmarks.

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
  with resets as (
    select sensor_id, reset_at, anchor_dry_bound, anchor_wet_bound
    from public.msv2_sensor_calibration_reset
    where receiver_id = p_receiver_id
  ),
  raw_windowed as (
    select h.sensor_id, h.moisture::numeric as moisture, h.received_at
    from public.msv2_sensor_history h
    left join resets r on r.sensor_id = h.sensor_id
    where h.receiver_id = p_receiver_id
      and h.received_at >= greatest(
        now() - (p_lookback_days || ' days')::interval,
        coalesce(r.reset_at, '-infinity'::timestamptz)
      )
      and h.moisture is not null
  ),
  bucketed as (
    select
      sensor_id,
      date_bin(interval '5 minutes', received_at, timestamptz '2000-01-01 00:00:00+00') as bucket_at,
      percentile_cont(0.50) within group (order by moisture) as moisture
    from raw_windowed
    group by sensor_id, date_bin(interval '5 minutes', received_at, timestamptz '2000-01-01 00:00:00+00')
  ),
  series as (
    select
      sensor_id,
      bucket_at,
      moisture,
      lag(moisture) over (partition by sensor_id order by bucket_at) as previous_moisture,
      bucket_at - lag(bucket_at) over (partition by sensor_id order by bucket_at) as gap
    from bucketed
  ),
  bounds as (
    select
      sensor_id,
      count(*) as sample_count,
      min(bucket_at) as oldest_reading_at,
      percentile_cont(0.05) within group (order by moisture) as dry_bound,
      percentile_cont(0.95) within group (order by moisture) as wet_bound
    from bucketed
    group by sensor_id
  ),
  rises as (
    select
      s.sensor_id,
      s.bucket_at as received_at,
      s.moisture - s.previous_moisture as delta,
      s.previous_moisture as cycle_dry_bound,
      (
        select max(after_bucket.moisture)
        from bucketed after_bucket
        where after_bucket.sensor_id = s.sensor_id
          and after_bucket.bucket_at between s.bucket_at and s.bucket_at + interval '3 hours'
      ) as cycle_wet_bound
    from series s
    join bounds b on b.sensor_id = s.sensor_id
    where s.previous_moisture is not null
      -- Do not interpret a reconnect after a long history gap as a watering step.
      and s.gap <= interval '30 minutes'
      and (b.wet_bound - b.dry_bound) > 0
      -- A real watering step should traverse most of the observed dry-to-wet span.
      -- The old 15% threshold classified ordinary 2-unit noise as watering on probes
      -- with narrow ranges. 80% accepts the shared-potted probes' real 15/14 and
      -- 431/327 rises while rejecting their ordinary noise.
      and (s.moisture - s.previous_moisture) >= 0.80 * (b.wet_bound - b.dry_bound)
  )
  select jsonb_agg(
    jsonb_build_object(
      'sensorId', b.sensor_id,
      -- sampleCount now means five-minute time buckets, not duplicated upload rows.
      'sampleCount', b.sample_count,
      'oldestReadingAt', b.oldest_reading_at,
      'dryBound', round(b.dry_bound),
      'wetBound', round(b.wet_bound),
      'anchorDryBound', r.anchor_dry_bound,
      'anchorWetBound', r.anchor_wet_bound,
      'rises', coalesce((
        select jsonb_agg(
          jsonb_build_object(
            'at', r2.received_at,
            'delta', round(r2.delta),
            'dryBound', round(r2.cycle_dry_bound),
            'wetBound', round(r2.cycle_wet_bound)
          )
          order by r2.received_at
        )
        from rises r2
        where r2.sensor_id = b.sensor_id
          and r2.cycle_wet_bound > r2.cycle_dry_bound
      ), '[]'::jsonb)
    )
  )
  into v_result
  from bounds b
  left join resets r on r.sensor_id = b.sensor_id;

  return coalesce(v_result, '[]'::jsonb);
end;
$$;

revoke execute on function public.msv2_read_sensor_auto_calibration(text, integer) from public;
grant execute on function public.msv2_read_sensor_auto_calibration(text, integer) to service_role;
