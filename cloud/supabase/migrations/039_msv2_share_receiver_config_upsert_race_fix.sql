-- msv2_upsert_share_receiver_config had the same lost-update race that
-- 033_msv2_display_settings_upsert_conflict_fix.sql closed for
-- msv2_upsert_receiver_display_settings, but was never given the same fix: v_next_version
-- was computed via a plain `select greatest(...) + 1` with no lock, then written
-- unconditionally via `on conflict (receiver_id) do update`. Two concurrent share-config
-- writes for the same receiver (e.g. two household members' phones) can both read the same
-- current version, both compute the same v_next_version, and the second commit silently
-- clobbers the first's config with no version-conflict surfaced to either caller. Add the
-- same pg_advisory_xact_lock serialization 033 already uses, in its own lock namespace so it
-- can never collide with the display-settings lock on the same receiver_id.

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

  perform pg_advisory_xact_lock(hashtextextended('msv2-share-config:' || p_receiver_id, 0));

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

revoke execute on function public.msv2_upsert_share_receiver_config(text, text, jsonb) from public, anon, authenticated;
grant execute on function public.msv2_upsert_share_receiver_config(text, text, jsonb) to service_role;
