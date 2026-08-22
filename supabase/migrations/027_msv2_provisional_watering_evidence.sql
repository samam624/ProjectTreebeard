-- Let newly arriving post-water data improve calibration before the 6-12 hour retention
-- window is complete, but at deliberately low evidence weight. Mature events still must
-- pass migration 026's persistence test. This makes the model responsive to continuous
-- uploads without permanently teaching it short oscillator/reconnect plateaus.

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
      post.max_moisture as post_max,
      later.sample_count as later_sample_count,
      later.median_moisture as later_median
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
    cross join lateral (
      select
        count(*) as sample_count,
        percentile_cont(0.50) within group (order by x.moisture) as median_moisture
      from bucketed x
      where x.sensor_id = r.sensor_id
        and x.bucket_at >= r.bucket_at + interval '6 hours'
        and x.bucket_at <= r.bucket_at + interval '12 hours'
    ) later
  ),
  valid_rises as (
    select
      c.sensor_id,
      c.bucket_at as received_at,
      c.moisture - c.previous_moisture as delta,
      0.75 * c.previous_moisture + 0.25 * c.pre_p20 as cycle_dry_bound,
      c.post_max as cycle_wet_bound,
      c.pre_sample_count,
      c.post_sample_count,
      c.later_sample_count,
      c.post_p80 - c.pre_median as sustained_delta,
      c.later_median - c.pre_median as retained_delta,
      c.later_sample_count >= 12
        and (c.later_median - c.pre_median) >= 0.20 * (c.fallback_wet_bound - c.fallback_dry_bound)
        as persistence_validated
    from contextual_rises c
    where c.pre_sample_count >= 6
      and c.post_sample_count >= 6
      and (c.post_p80 - c.pre_median) >= 0.50 * (c.fallback_wet_bound - c.fallback_dry_bound)
      and abs(c.previous_moisture - c.pre_median) <= 0.50 * (c.fallback_wet_bound - c.fallback_dry_bound)
      and c.post_max > c.previous_moisture
      and (
        -- Confirmed mature event.
        (c.later_sample_count >= 12
          and (c.later_median - c.pre_median) >= 0.20 * (c.fallback_wet_bound - c.fallback_dry_bound))
        -- Or a still-maturing recent event. The edge function gives this only 25% weight
        -- and it disappears if the later window eventually disproves soil retention.
        or (c.later_sample_count < 12 and c.bucket_at >= now() - interval '12 hours')
      )
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
            'laterSampleCount', v.later_sample_count,
            'sustainedDelta', round(v.sustained_delta),
            'retainedDelta', round(v.retained_delta),
            'persistenceValidated', v.persistence_validated
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
