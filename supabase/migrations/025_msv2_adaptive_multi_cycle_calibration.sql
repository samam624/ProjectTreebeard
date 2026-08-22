-- Continuously learned moisture calibration.
--
-- Migration 024 fixed upload-cadence bias by resampling history into one median per
-- five-minute bucket, but its watering landmarks still came from a single large step and
-- receiver-snapshot selected only the newest cycle. That leaves two problems:
--   * a one-bucket electrical/reconnect transient can look like a watering; and
--   * one atypical watering can define the entire displayed 0-100% range.
--
-- This version uses the surrounding time series to validate each candidate watering:
--   * the step must be followed by a sustained rise in the next three hours relative to
--     the preceding two-hour baseline;
--   * both windows must contain enough five-minute buckets;
--   * the pre-watering step must agree with its surrounding baseline; and
--   * dry/wet landmarks are built from five-minute medians plus the supported windows,
--     not isolated raw uploads.
--
-- Every valid event is returned with evidence counts and sustained-rise strength. The
-- edge function combines all of them using an adaptive, recency-weighted robust model;
-- the time-weighted 5th/95th percentiles remain a weak continuously-updated prior.

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
  bucketed as materialized (
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
  rough_rises as (
    select s.*, b.dry_bound as fallback_dry_bound, b.wet_bound as fallback_wet_bound
    from series s
    join bounds b on b.sensor_id = s.sensor_id
    where s.previous_moisture is not null
      and s.gap <= interval '30 minutes'
      and b.wet_bound > b.dry_bound
      -- A 20% first step catches watering applied in several pours. The sustained-window
      -- test below rejects ordinary positive noise that clears this deliberately loose gate.
      and (s.moisture - s.previous_moisture) >= 0.20 * (b.wet_bound - b.dry_bound)
  ),
  contextual_rises as (
    select
      r.*,
      pre.sample_count as pre_sample_count,
      pre.median_moisture as pre_median,
      pre.p20_moisture as pre_p20,
      post.sample_count as post_sample_count,
      post.p80_moisture as post_p80,
      post.max_moisture as post_max
    from rough_rises r
    cross join lateral (
      select
        count(*) as sample_count,
        percentile_cont(0.50) within group (order by x.moisture) as median_moisture,
        percentile_cont(0.20) within group (order by x.moisture) as p20_moisture
      from bucketed x
      where x.sensor_id = r.sensor_id
        and x.bucket_at >= r.bucket_at - interval '2 hours'
        and x.bucket_at < r.bucket_at
    ) pre
    cross join lateral (
      select
        count(*) as sample_count,
        percentile_cont(0.80) within group (order by x.moisture) as p80_moisture,
        max(x.moisture) as max_moisture
      from bucketed x
      where x.sensor_id = r.sensor_id
        and x.bucket_at >= r.bucket_at
        and x.bucket_at <= r.bucket_at + interval '3 hours'
    ) post
  ),
  valid_rises as (
    select
      c.sensor_id,
      c.bucket_at as received_at,
      c.moisture - c.previous_moisture as delta,
      -- The five-minute pre-step median retains the actual just-before-watering level;
      -- blending in the robust lower pre-window quantile makes it less sensitive to a
      -- single bucket without erasing a legitimate last-minute low.
      0.75 * c.previous_moisture + 0.25 * c.pre_p20 as cycle_dry_bound,
      -- max() is over five-minute medians rather than uploads, so the true fast wet peak
      -- is preserved while individual radio/ADC spikes have already been removed.
      c.post_max as cycle_wet_bound,
      c.pre_sample_count,
      c.post_sample_count,
      c.post_p80 - c.pre_median as sustained_delta
    from contextual_rises c
    where c.pre_sample_count >= 6
      and c.post_sample_count >= 6
      -- A real watering changes the sustained post-water level, not just one bucket.
      and (c.post_p80 - c.pre_median) >= 0.50 * (c.fallback_wet_bound - c.fallback_dry_bound)
      -- Reject a candidate whose just-before value disagrees radically with the previous
      -- two hours (the exact signature of the probe-3 August 9 reconnect transient).
      and abs(c.previous_moisture - c.pre_median) <= 0.50 * (c.fallback_wet_bound - c.fallback_dry_bound)
      and c.post_max > c.previous_moisture
  )
  select jsonb_agg(
    jsonb_build_object(
      'sensorId', b.sensor_id,
      'sampleCount', b.sample_count,
      'oldestReadingAt', b.oldest_reading_at,
      'dryBound', round(b.dry_bound),
      'wetBound', round(b.wet_bound),
      'anchorDryBound', r.anchor_dry_bound,
      'anchorWetBound', r.anchor_wet_bound,
      'rises', coalesce((
        select jsonb_agg(
          jsonb_build_object(
            'at', v.received_at,
            'delta', round(v.delta),
            'dryBound', round(v.cycle_dry_bound),
            'wetBound', round(v.cycle_wet_bound),
            'preSampleCount', v.pre_sample_count,
            'postSampleCount', v.post_sample_count,
            'sustainedDelta', round(v.sustained_delta)
          )
          order by v.received_at
        )
        from valid_rises v
        where v.sensor_id = b.sensor_id
          and v.cycle_wet_bound > v.cycle_dry_bound
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
