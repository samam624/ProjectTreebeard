-- 2026-08-17 auto-calibration redesign (see docs/STATUS_HANDOFF.md's "2026-08-16
-- Auto-Calibration ('Learning Mode') Redesign" section, and the follow-up discussion
-- dated the same day in the chat log referenced from that doc):
--  1. A per-sensor calibration reset epoch, so a household can force a sensor back into
--     "learning" mode (e.g. after moving a probe to a new plant/pot) without destroying
--     real History-tab data - msv2_read_sensor_auto_calibration's percentile window now
--     clamps its lower bound to the reset time instead of just now() - lookback_days.
--  2. An anchored floor clamp on the learned dry bound: chronic under-watering otherwise
--     drags the rolling 5th-percentile floor down indefinitely (the alert threshold
--     chases the neglect instead of catching it). There's no absolute ground-truth dry
--     value to clamp against - it differs per plant/pot/soil (and across this
--     deployment's three different probes specifically, per the user), so each sensor is
--     anchored to its own first full-window calibration instead of any universal number.
--     The margin is a percentage of that same sensor's own anchored range
--     (AUTOCAL_DRY_FLOOR_MARGIN_PCT in receiver-snapshot/index.ts), not a flat raw-unit
--     constant, so it scales automatically across probes on very different raw scales.
--     The anchor is captured once the calibration window is full (p_lookback_days, 21 by
--     default) rather than at the earlier 3-day "ready" gate - the first 21 days are
--     meant to run unclamped so multiple real watering cycles get observed before
--     anything is locked in.

create table if not exists public.msv2_sensor_calibration_reset (
  receiver_id text not null,
  sensor_id integer not null,
  -- Both nullable and no default: a row can exist purely to hold an anchor (no manual
  -- reset ever happened) without implying a calibration-window clamp, and vice versa -
  -- see msv2_reset_sensor_calibration and msv2_set_sensor_calibration_anchor_if_unset.
  -- anchor_dry_bound/anchor_wet_bound are always set together (see that function) - the
  -- pair defines the anchored range the floor-clamp margin is a percentage of.
  reset_at timestamptz null,
  anchor_dry_bound numeric null,
  anchor_wet_bound numeric null,
  primary key (receiver_id, sensor_id)
);

alter table public.msv2_sensor_calibration_reset enable row level security;

-- Same share-token auth pattern as msv2_clear_share_sensor_history (016): resolves the
-- token to a household, confirms that household actually has this receiver claimed, then
-- upserts today's reset time as the new floor for that one sensor's calibration window.
-- Also clears both anchor columns - a reset means "this is a different plant/pot now", so
-- the old floor anchor is exactly as stale as the old learned range, and a fresh one gets
-- captured automatically once the newly-reset window is full again.
create or replace function public.msv2_reset_sensor_calibration(
  p_share_token text,
  p_receiver_id text,
  p_sensor_id integer
)
returns timestamptz
language plpgsql
security definer
set search_path = public, extensions
as $$
declare
  v_household_id uuid;
  v_reset_at timestamptz := now();
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

  insert into public.msv2_sensor_calibration_reset (receiver_id, sensor_id, reset_at, anchor_dry_bound, anchor_wet_bound)
  values (p_receiver_id, p_sensor_id, v_reset_at, null, null)
  on conflict (receiver_id, sensor_id) do update
    set reset_at = excluded.reset_at, anchor_dry_bound = null, anchor_wet_bound = null;

  return v_reset_at;
end;
$$;

revoke execute on function public.msv2_reset_sensor_calibration(text, text, integer) from public;
grant execute on function public.msv2_reset_sensor_calibration(text, text, integer) to anon, authenticated, service_role;

-- Internal only (service_role) - called from receiver-snapshot/index.ts's deriveAutoCal
-- flow the first time a sensor's calibration window is full (see AUTOCAL_LOOKBACK_DAYS)
-- with no anchor on record yet. Dry and wet are always set together, since the floor
-- margin is a percentage of the anchored (wet - dry) range, not a flat number - a
-- half-set anchor pair would make that undefined. The "where anchor_dry_bound is null"
-- guard makes this idempotent/race-safe: concurrent callers (or a retried request after a
-- transient failure) never overwrite an anchor that's already set, only ever fill one in
-- when genuinely absent.
create or replace function public.msv2_set_sensor_calibration_anchor_if_unset(
  p_receiver_id text,
  p_sensor_id integer,
  p_anchor_dry_bound numeric,
  p_anchor_wet_bound numeric
)
returns void
language plpgsql
security definer
set search_path = public
as $$
begin
  insert into public.msv2_sensor_calibration_reset (receiver_id, sensor_id, anchor_dry_bound, anchor_wet_bound)
  values (p_receiver_id, p_sensor_id, p_anchor_dry_bound, p_anchor_wet_bound)
  on conflict (receiver_id, sensor_id) do update
    set anchor_dry_bound = excluded.anchor_dry_bound, anchor_wet_bound = excluded.anchor_wet_bound
    where msv2_sensor_calibration_reset.anchor_dry_bound is null;
end;
$$;

revoke execute on function public.msv2_set_sensor_calibration_anchor_if_unset(text, integer, numeric, numeric) from public;
grant execute on function public.msv2_set_sensor_calibration_anchor_if_unset(text, integer, numeric, numeric) to service_role;

-- Rebuild of 017's function: adds the reset-epoch clamp above, plus returns each
-- sensor's anchor_dry_bound so receiver-snapshot's deriveAutoCal can apply/maintain the
-- floor clamp. rises/watering-event detection is unchanged (still feeds deriveAutoCal's
-- daysSinceLastWatering - kept as an informational stat even though the "overdue"
-- forcing/alerting behavior built on top of it was removed this same session).
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
  windowed as (
    select
      h.sensor_id,
      h.moisture,
      h.received_at,
      h.moisture - lag(h.moisture) over (partition by h.sensor_id order by h.received_at) as delta,
      h.received_at - lag(h.received_at) over (partition by h.sensor_id order by h.received_at) as gap
    from public.msv2_sensor_history h
    left join resets r on r.sensor_id = h.sensor_id
    where h.receiver_id = p_receiver_id
      and h.received_at >= greatest(
        now() - (p_lookback_days || ' days')::interval,
        coalesce(r.reset_at, '-infinity'::timestamptz)
      )
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
      'anchorDryBound', r.anchor_dry_bound,
      'anchorWetBound', r.anchor_wet_bound,
      'rises', coalesce((
        select jsonb_agg(jsonb_build_object('at', r2.received_at, 'delta', round(r2.delta)) order by r2.received_at)
        from rises r2
        where r2.sensor_id = b.sensor_id
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
