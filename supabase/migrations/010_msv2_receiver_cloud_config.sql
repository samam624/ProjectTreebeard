create table if not exists public.msv2_receiver_configs (
  receiver_id text primary key references public.msv2_receivers(receiver_id) on delete cascade,
  config jsonb not null,
  config_version integer not null,
  updated_at timestamptz not null default now(),
  updated_by text not null default 'cloud'
);

alter table public.msv2_receiver_configs enable row level security;

create or replace function public.msv2_upsert_share_receiver_config(
  p_share_token text,
  p_receiver_id text,
  p_config jsonb
)
returns table (
  receiver_id text,
  config jsonb,
  config_version integer,
  updated_at timestamptz,
  updated_by text
)
language plpgsql
security definer
set search_path = public, extensions
as $$
declare
  v_household_id uuid;
  v_next_version integer;
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

  select greatest(
    coalesce((select config_version from public.msv2_receiver_configs where receiver_id = p_receiver_id), 0),
    coalesce((select config_version from public.msv2_latest_snapshots where receiver_id = p_receiver_id), 0)
  ) + 1
    into v_next_version;

  p_config := jsonb_set(
    coalesce(p_config, '{}'::jsonb),
    '{_meta}',
    jsonb_build_object(
      'schema', 1,
      'version', v_next_version,
      'updatedMs', 0,
      'updatedBy', 'cloud'
    ),
    true
  );

  insert into public.msv2_receiver_configs (
    receiver_id,
    config,
    config_version,
    updated_at,
    updated_by
  )
  values (
    p_receiver_id,
    p_config,
    v_next_version,
    now(),
    'cloud'
  )
  on conflict (receiver_id) do update
    set config = excluded.config,
        config_version = excluded.config_version,
        updated_at = excluded.updated_at,
        updated_by = excluded.updated_by;

  return query
    select
      c.receiver_id,
      c.config,
      c.config_version,
      c.updated_at,
      c.updated_by
    from public.msv2_receiver_configs c
    where c.receiver_id = p_receiver_id;
end;
$$;

create or replace function public.msv2_read_share_receiver_configs(
  p_share_token text
)
returns table (
  receiver_id text,
  config jsonb,
  config_version integer,
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
  select t.household_id
    into v_household_id
  from public.msv2_household_share_tokens t
  where t.token_hash = public.msv2_hash_secret(p_share_token)
    and t.revoked_at is null;

  if v_household_id is null then
    raise exception 'invalid share token';
  end if;

  return query
    select
      c.receiver_id,
      coalesce(rc.config, s.snapshot->'config') as config,
      coalesce(rc.config_version, s.config_version, 0) as config_version,
      coalesce(rc.updated_at, s.received_at) as updated_at,
      coalesce(rc.updated_by, s.snapshot->'config'->>'updatedBy', 'receiver') as updated_by
    from public.msv2_receiver_claims c
    join public.msv2_latest_snapshots s
      on s.receiver_id = c.receiver_id
    left join public.msv2_receiver_configs rc
      on rc.receiver_id = c.receiver_id
    where c.household_id = v_household_id
    order by c.receiver_id;
end;
$$;

create or replace function public.msv2_read_receiver_pending_config(
  p_receiver_id text,
  p_current_version integer default 0
)
returns table (
  receiver_id text,
  config jsonb,
  config_version integer,
  updated_at timestamptz,
  updated_by text
)
language sql
security definer
set search_path = public
as $$
  select
    c.receiver_id,
    c.config,
    c.config_version,
    c.updated_at,
    c.updated_by
  from public.msv2_receiver_configs c
  where c.receiver_id = p_receiver_id
    and c.config_version > coalesce(p_current_version, 0)
  limit 1;
$$;

grant execute on function public.msv2_upsert_share_receiver_config(text, text, jsonb) to anon, authenticated, service_role;
grant execute on function public.msv2_read_share_receiver_configs(text) to anon, authenticated, service_role;
grant execute on function public.msv2_read_receiver_pending_config(text, integer) to anon, authenticated, service_role;
