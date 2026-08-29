-- Preserve every receiver-observed probe reading across slower snapshot uploads.
-- observation_id makes retries idempotent when the Edge Function committed rows but the
-- receiver lost the HTTP acknowledgement. p_received_at records when BLE saw the reading,
-- reconstructed from the queued age, rather than collapsing a whole batch onto upload time.

alter table public.msv2_sensor_history
  add column if not exists observation_id text;

create unique index if not exists msv2_sensor_history_receiver_observation_idx
  on public.msv2_sensor_history(receiver_id, observation_id)
  where observation_id is not null;

drop function if exists public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer, integer, integer, bigint);

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
  p_received_at timestamptz default null
)
returns void
language plpgsql
security definer
set search_path = public
as $$
declare
  v_scale integer := greatest(1, coalesce(p_moisture_scale, 1));
  v_previous_scale integer;
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

  select h.moisture_scale
    into v_previous_scale
  from public.msv2_sensor_history h
  where h.receiver_id = p_receiver_id
    and h.sensor_id = p_sensor_id
  order by h.id desc
  limit 1;

  if v_previous_scale is not null and v_previous_scale <> v_scale then
    insert into public.msv2_sensor_calibration_reset (
      receiver_id, sensor_id, reset_at, anchor_dry_bound, anchor_wet_bound
    )
    values (p_receiver_id, p_sensor_id, v_received_at, null, null)
    on conflict (receiver_id, sensor_id) do update
      set reset_at = excluded.reset_at,
          anchor_dry_bound = null,
          anchor_wet_bound = null;
  end if;

  insert into public.msv2_sensor_history (
    receiver_id, sensor_id, wake, moisture, display_moisture, moisture_scale,
    raw_moisture_khz, raw_fdc_counts, battery_percent, rssi, age_ms,
    observation_id, received_at
  )
  values (
    p_receiver_id, p_sensor_id, p_wake, p_moisture, p_display_moisture, v_scale,
    p_raw_moisture_khz, p_raw_fdc_counts, p_battery_percent, p_rssi, p_age_ms,
    p_observation_id, v_received_at
  )
  on conflict do nothing;
end;
$$;

revoke execute on function public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer, integer, integer, bigint, text, timestamptz) from public, anon, authenticated;
grant execute on function public.msv2_store_sensor_reading(text, integer, integer, integer, integer, integer, integer, integer, integer, integer, bigint, text, timestamptz) to service_role;
