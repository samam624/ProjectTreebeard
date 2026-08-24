-- Two fixes from a full-repo hostile review (2026-08-23):
--
-- 1. msv2_reset_sensor_calibration (023) is the only RPC since the 020 lockdown pass to be
-- granted execute to `anon, authenticated` in addition to `service_role` - every sibling
-- function follows the "service_role only, edge function is the sole real caller" pattern
-- 020 itself established for exactly this reason. supabase/functions/plant-dashboard/
-- index.ts's shareAutocalResetApi already uses the service-role key, so the public grant is
-- unnecessary attack surface (not a cross-household IDOR - the function still checks
-- token_hash/household linkage - just an avoidable direct-RPC path bypassing the edge
-- function's method/CORS gating).
--
-- 2. The "look up this sensor's previous moisture_scale, and if it changed, reset its
-- calibration anchor" business logic (introduced in 029) has been hand-copied verbatim into
-- msv2_store_sensor_reading's body three times since (029, 030, 032) purely because each
-- migration needed to append new trailing parameters to that function. Extracted into a
-- small helper so any future correctness fix to the reset logic only has one place to land.

revoke execute on function public.msv2_reset_sensor_calibration(text, text, integer) from anon, authenticated;

create or replace function public.msv2_apply_scale_transition_reset(
  p_receiver_id text,
  p_sensor_id integer,
  p_scale integer,
  p_received_at timestamptz
)
returns void
language plpgsql
security definer
set search_path = public
as $$
declare
  v_previous_scale integer;
begin
  select h.moisture_scale
    into v_previous_scale
  from public.msv2_sensor_history h
  where h.receiver_id = p_receiver_id
    and h.sensor_id = p_sensor_id
  order by h.id desc
  limit 1;

  if v_previous_scale is not null and v_previous_scale <> p_scale then
    insert into public.msv2_sensor_calibration_reset (
      receiver_id, sensor_id, reset_at, anchor_dry_bound, anchor_wet_bound
    )
    values (p_receiver_id, p_sensor_id, p_received_at, null, null)
    on conflict (receiver_id, sensor_id) do update
      set reset_at = excluded.reset_at,
          anchor_dry_bound = null,
          anchor_wet_bound = null;
  end if;
end;
$$;

revoke execute on function public.msv2_apply_scale_transition_reset(text, integer, integer, timestamptz) from public, anon, authenticated;
grant execute on function public.msv2_apply_scale_transition_reset(text, integer, integer, timestamptz) to service_role;

-- Same signature as 032 - no drop/param change, just replacing the inline scale-transition
-- block with a call to the new helper above.
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
  p_raw_charging boolean default null
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
  -- A timed-out receiver retries the exact same queue prefix. Return before calibration
  -- side effects as well as before the insert so retries are fully idempotent.
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
    observation_id, received_at, raw_battery_mv, raw_charging
  )
  values (
    p_receiver_id, p_sensor_id, p_wake, p_moisture, p_display_moisture, v_scale,
    p_raw_moisture_khz, p_raw_fdc_counts, p_battery_percent, p_rssi, p_age_ms,
    p_observation_id, v_received_at, p_raw_battery_mv, p_raw_charging
  )
  on conflict do nothing;
end;
$$;

revoke execute on function public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer, integer, integer, bigint, text, timestamptz, integer, boolean) from public, anon, authenticated;
grant execute on function public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer, integer, integer, bigint, text, timestamptz, integer, boolean) to service_role;
