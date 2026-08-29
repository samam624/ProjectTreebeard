-- Fix the receiver display-settings POST path. The original PL/pgSQL function used
-- `on conflict (receiver_id)` while also declaring a RETURNS TABLE output variable named
-- receiver_id. PostgreSQL can resolve that as an ambiguous PL/pgSQL reference at runtime,
-- so GET works but the first receiver-touch upsert returns HTTP 400 through the Edge Function.
-- A named constraint is unambiguous and preserves the existing API/return contract.

create or replace function public.msv2_upsert_receiver_display_settings(
  p_receiver_id text,
  p_brightness_percent integer,
  p_screen_on boolean,
  p_updated_by text default 'receiver'
)
returns table (
  receiver_id text,
  brightness_percent smallint,
  screen_on boolean,
  settings_version bigint,
  updated_at timestamptz,
  updated_by text
)
language plpgsql
security definer
set search_path = public
as $$
declare
  v_next_version bigint;
begin
  if p_brightness_percent is null or p_brightness_percent < 1 or p_brightness_percent > 100 then
    raise exception 'brightnessPercent must be between 1 and 100';
  end if;
  if p_screen_on is null then
    raise exception 'screenOn is required';
  end if;
  if not exists (select 1 from public.msv2_receivers r where r.receiver_id = p_receiver_id) then
    raise exception 'receiver not found';
  end if;

  perform pg_advisory_xact_lock(hashtextextended('msv2-display:' || p_receiver_id, 0));

  select greatest(
    coalesce((
      select d.settings_version
      from public.msv2_receiver_display_settings d
      where d.receiver_id = p_receiver_id
    ), 0),
    coalesce((
      select case
        when s.snapshot->'display'->>'settingsVersion' ~ '^[0-9]+$'
          then (s.snapshot->'display'->>'settingsVersion')::bigint
        else 0
      end
      from public.msv2_latest_snapshots s
      where s.receiver_id = p_receiver_id
    ), 0)
  ) + 1 into v_next_version;

  insert into public.msv2_receiver_display_settings (
    receiver_id,
    brightness_percent,
    screen_on,
    settings_version,
    updated_at,
    updated_by
  ) values (
    p_receiver_id,
    p_brightness_percent,
    p_screen_on,
    v_next_version,
    now(),
    left(coalesce(nullif(p_updated_by, ''), 'receiver'), 32)
  )
  on conflict on constraint msv2_receiver_display_settings_pkey do update
    set brightness_percent = excluded.brightness_percent,
        screen_on = excluded.screen_on,
        settings_version = excluded.settings_version,
        updated_at = excluded.updated_at,
        updated_by = excluded.updated_by;

  return query
    select d.receiver_id, d.brightness_percent, d.screen_on, d.settings_version, d.updated_at, d.updated_by
    from public.msv2_receiver_display_settings d
    where d.receiver_id = p_receiver_id;
end;
$$;

revoke execute on function public.msv2_upsert_receiver_display_settings(text, integer, boolean, text)
  from public, anon, authenticated;
grant execute on function public.msv2_upsert_receiver_display_settings(text, integer, boolean, text)
  to service_role;
