-- Display controls deliberately live outside msv2_receiver_configs/ui.json. Brightness is
-- written by both the receiver touchscreen and the household phone app; a dedicated,
-- server-versioned row prevents either writer from replacing unrelated plant layout data.

create table if not exists public.msv2_receiver_display_settings (
  receiver_id text primary key references public.msv2_receivers(receiver_id) on delete cascade,
  brightness_percent smallint not null default 75 check (brightness_percent between 1 and 100),
  screen_on boolean not null default true,
  settings_version bigint not null,
  updated_at timestamptz not null default now(),
  updated_by text not null default 'receiver'
);

alter table public.msv2_receiver_display_settings enable row level security;

-- Called only by trusted Edge Functions after receiver-token authentication. The advisory
-- transaction lock makes version allocation atomic even when a phone write and physical touch
-- arrive at nearly the same instant.
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
  -- Use the named constraint instead of `on conflict (receiver_id)`. In PL/pgSQL this
  -- function's RETURNS TABLE output also defines a `receiver_id` variable, making the
  -- unqualified conflict target ambiguous at runtime on PostgreSQL.
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

create or replace function public.msv2_upsert_share_receiver_display_settings(
  p_share_token text,
  p_receiver_id text,
  p_brightness_percent integer,
  p_screen_on boolean
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
set search_path = public, extensions
as $$
declare
  v_household_id uuid;
begin
  select t.household_id into v_household_id
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

  return query
    select *
    from public.msv2_upsert_receiver_display_settings(
      p_receiver_id,
      p_brightness_percent,
      p_screen_on,
      'phone'
    );
end;
$$;

create or replace function public.msv2_read_receiver_pending_display_settings(
  p_receiver_id text,
  p_current_version bigint default 0
)
returns table (
  receiver_id text,
  brightness_percent smallint,
  screen_on boolean,
  settings_version bigint,
  updated_at timestamptz,
  updated_by text
)
language sql
security definer
set search_path = public
as $$
  select d.receiver_id, d.brightness_percent, d.screen_on, d.settings_version, d.updated_at, d.updated_by
  from public.msv2_receiver_display_settings d
  where d.receiver_id = p_receiver_id
    and d.settings_version > coalesce(p_current_version, 0)
  limit 1;
$$;

create or replace function public.msv2_read_share_receiver_display_settings(
  p_share_token text
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
set search_path = public, extensions
as $$
declare
  v_household_id uuid;
begin
  select t.household_id into v_household_id
  from public.msv2_household_share_tokens t
  where t.token_hash = public.msv2_hash_secret(p_share_token)
    and t.revoked_at is null;

  if v_household_id is null then
    raise exception 'invalid share token';
  end if;

  return query
    select
      c.receiver_id,
      coalesce(
        d.brightness_percent,
        case when s.snapshot->'display'->>'brightnessPercent' ~ '^[0-9]+$'
          then (s.snapshot->'display'->>'brightnessPercent')::smallint end,
        75::smallint
      ),
      coalesce(d.screen_on, (s.snapshot->'display'->>'screenOn')::boolean, true),
      coalesce(
        d.settings_version,
        case when s.snapshot->'display'->>'settingsVersion' ~ '^[0-9]+$'
          then (s.snapshot->'display'->>'settingsVersion')::bigint end,
        0::bigint
      ),
      coalesce(d.updated_at, s.received_at, now()),
      coalesce(d.updated_by, s.snapshot->'display'->>'updatedBy', 'receiver')
    from public.msv2_receiver_claims c
    left join public.msv2_receiver_display_settings d on d.receiver_id = c.receiver_id
    left join public.msv2_latest_snapshots s on s.receiver_id = c.receiver_id
    where c.household_id = v_household_id
    order by c.receiver_id;
end;
$$;

revoke execute on function public.msv2_upsert_receiver_display_settings(text, integer, boolean, text) from public, anon, authenticated;
revoke execute on function public.msv2_upsert_share_receiver_display_settings(text, text, integer, boolean) from public, anon, authenticated;
revoke execute on function public.msv2_read_receiver_pending_display_settings(text, bigint) from public, anon, authenticated;
revoke execute on function public.msv2_read_share_receiver_display_settings(text) from public, anon, authenticated;

grant execute on function public.msv2_upsert_receiver_display_settings(text, integer, boolean, text) to service_role;
grant execute on function public.msv2_upsert_share_receiver_display_settings(text, text, integer, boolean) to service_role;
grant execute on function public.msv2_read_receiver_pending_display_settings(text, bigint) to service_role;
grant execute on function public.msv2_read_share_receiver_display_settings(text) to service_role;
