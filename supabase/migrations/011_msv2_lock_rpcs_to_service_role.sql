-- Security hardening: close the direct-PostgREST-RPC bypass around the edge functions.
--
-- Every RPC below was previously granted to `anon, authenticated`, which meant anyone with
-- the public API key could call `/rest/v1/rpc/<fn>` directly and skip every auth check written
-- into the Deno edge functions (JWT verification in claim-receiver, token verification in
-- receiver-snapshot, etc). All three edge functions already use the service_role key internally,
-- so the anon/authenticated grants served no legitimate purpose. Verified: nothing in the
-- firmware or web dashboard calls Supabase REST directly; everything goes through the edge
-- functions.
--
-- Note: msv2_receivers / msv2_latest_snapshots / msv2_household_share_tokens / msv2_receiver_configs
-- all have `ENABLE ROW LEVEL SECURITY` with no policies defined, and there are no table-level
-- GRANTs to anon/authenticated anywhere in this migration history. That combination already
-- denies direct table access at the privilege layer before RLS is even consulted, so it's a
-- correct deny-all backstop as-is -- no table policies are added here.

revoke execute on function public.msv2_store_latest_snapshot(text, jsonb, integer, integer) from anon, authenticated;
revoke execute on function public.msv2_read_latest_snapshots(text, integer) from anon, authenticated;
revoke execute on function public.msv2_claim_receiver(uuid, text, text, text) from anon, authenticated;
revoke execute on function public.msv2_read_user_latest_snapshots(uuid) from anon, authenticated;
revoke execute on function public.msv2_hash_secret(text) from anon, authenticated;
revoke execute on function public.msv2_register_receiver(text, text, text, text) from anon, authenticated;
revoke execute on function public.msv2_verify_receiver_api_token(text, text) from anon, authenticated;
revoke execute on function public.msv2_issue_household_share(text, text, text) from anon, authenticated;
revoke execute on function public.msv2_read_share_latest_snapshots(text) from anon, authenticated;
revoke execute on function public.msv2_upsert_share_receiver_config(text, text, jsonb) from anon, authenticated;
revoke execute on function public.msv2_read_share_receiver_configs(text) from anon, authenticated;
revoke execute on function public.msv2_read_receiver_pending_config(text, integer) from anon, authenticated;

-- Generic per-key rate limiter, used below to blunt brute-forcing a claim code or squatting a
-- specific receiver_id via repeated registration attempts. Keyed by receiver_id, not caller IP
-- (no IP plumbing exists into these RPCs today) -- this does not stop an attacker who already has
-- a list of many valid receiver IDs, only repeated attempts against one target.
create table if not exists public.msv2_rate_limit_events (
  bucket_key text primary key,
  window_started_at timestamptz not null default now(),
  attempt_count integer not null default 0
);

alter table public.msv2_rate_limit_events enable row level security;

create or replace function public.msv2_check_rate_limit(
  p_bucket text,
  p_max_attempts integer,
  p_window_seconds integer
)
returns void
language plpgsql
security definer
set search_path = public
as $$
declare
  v_row public.msv2_rate_limit_events%rowtype;
begin
  insert into public.msv2_rate_limit_events (bucket_key, window_started_at, attempt_count)
  values (p_bucket, now(), 1)
  on conflict (bucket_key) do update
    set attempt_count = case
          when public.msv2_rate_limit_events.window_started_at < now() - make_interval(secs => p_window_seconds)
          then 1
          else public.msv2_rate_limit_events.attempt_count + 1
        end,
        window_started_at = case
          when public.msv2_rate_limit_events.window_started_at < now() - make_interval(secs => p_window_seconds)
          then now()
          else public.msv2_rate_limit_events.window_started_at
        end
  returning * into v_row;

  if v_row.attempt_count > p_max_attempts then
    raise exception 'rate limited, try again later';
  end if;
end;
$$;

-- Hash claim codes at rest, same sha256 pattern already used for receiver_secret/api_token
-- (public.msv2_hash_secret, introduced in 007/008). Nothing reads msv2_receivers.claim_code back
-- out to a client anywhere -- receiver-bootstrap echoes the *request's* claim code, not a DB read
-- -- so this is a purely internal storage/comparison change.
alter table public.msv2_receivers add column if not exists claim_code_hash text;

update public.msv2_receivers
  set claim_code_hash = public.msv2_hash_secret(claim_code)
  where claim_code is not null
    and claim_code_hash is null;

alter table public.msv2_receivers drop column if exists claim_code;

create or replace function public.msv2_touch_receiver(
  p_receiver_id text,
  p_claim_code text,
  p_build text,
  p_last_ip inet
)
returns void
language plpgsql
security definer
set search_path = public
as $$
begin
  insert into public.msv2_receivers (
    receiver_id,
    claim_code_hash,
    build,
    last_ip
  )
  values (
    p_receiver_id,
    case when p_claim_code is not null then public.msv2_hash_secret(p_claim_code) else null end,
    p_build,
    p_last_ip
  )
  on conflict (receiver_id) do update
    set claim_code_hash = coalesce(
          case when p_claim_code is not null then public.msv2_hash_secret(p_claim_code) else null end,
          public.msv2_receivers.claim_code_hash
        ),
        build = excluded.build,
        last_seen_at = now(),
        last_ip = excluded.last_ip;
end;
$$;

create or replace function public.msv2_claim_receiver(
  p_user_id uuid,
  p_receiver_id text,
  p_claim_code text,
  p_household_name text default 'My Plants'
)
returns table (
  household_id uuid,
  receiver_id text
)
language plpgsql
security definer
set search_path = public
as $$
declare
  v_receiver public.msv2_receivers%rowtype;
  v_household_id uuid;
begin
  perform public.msv2_check_rate_limit('claim:' || p_receiver_id, 8, 3600);

  select *
    into v_receiver
  from public.msv2_receivers
  where public.msv2_receivers.receiver_id = p_receiver_id;

  if not found then
    raise exception 'receiver not found';
  end if;

  if coalesce(v_receiver.claim_code_hash, '') <> coalesce(public.msv2_hash_secret(p_claim_code), '') then
    raise exception 'invalid claim code';
  end if;

  select c.household_id
    into v_household_id
  from public.msv2_receiver_claims c
  where c.receiver_id = p_receiver_id;

  if v_household_id is not null then
    if not exists (
      select 1 from public.msv2_household_members m
      where m.household_id = v_household_id
        and m.user_id = p_user_id
    ) then
      raise exception 'receiver already claimed';
    end if;
  else
    insert into public.msv2_households (name, created_by)
    values (coalesce(nullif(p_household_name, ''), 'My Plants'), p_user_id)
    returning msv2_households.household_id into v_household_id;

    insert into public.msv2_household_members (household_id, user_id, role)
    values (v_household_id, p_user_id, 'owner')
    on conflict do nothing;

    insert into public.msv2_receiver_claims (receiver_id, household_id, claimed_by)
    values (p_receiver_id, v_household_id, p_user_id);
  end if;

  household_id := v_household_id;
  receiver_id := p_receiver_id;
  return next;
end;
$$;

create or replace function public.msv2_register_receiver(
  p_receiver_id text,
  p_receiver_secret text,
  p_claim_code text,
  p_build text default null
)
returns table (
  receiver_id text,
  api_token text,
  api_token_hint text
)
language plpgsql
security definer
set search_path = public, extensions
as $$
declare
  v_existing public.msv2_receivers%rowtype;
  v_secret_hash text;
  v_api_token text;
  v_api_token_hash text;
  v_api_token_hint text;
begin
  perform public.msv2_check_rate_limit('register:' || p_receiver_id, 8, 3600);

  if nullif(trim(p_receiver_id), '') is null then
    raise exception 'receiver id required';
  end if;

  if nullif(trim(p_receiver_secret), '') is null then
    raise exception 'receiver secret required';
  end if;

  v_secret_hash := public.msv2_hash_secret(p_receiver_secret);

  select *
    into v_existing
  from public.msv2_receivers r
  where r.receiver_id = p_receiver_id;

  if found
     and v_existing.receiver_secret_hash is not null
     and v_existing.receiver_secret_hash <> v_secret_hash then
    raise exception 'receiver secret mismatch';
  end if;

  v_api_token := encode(gen_random_bytes(32), 'hex');
  v_api_token_hash := public.msv2_hash_secret(v_api_token);
  v_api_token_hint := right(v_api_token, 6);

  insert into public.msv2_receivers (
    receiver_id,
    claim_code_hash,
    build,
    receiver_secret_hash,
    receiver_api_token_hash,
    receiver_api_token_hint,
    cloud_registered_at,
    last_seen_at
  )
  values (
    p_receiver_id,
    case when nullif(trim(p_claim_code), '') is not null then public.msv2_hash_secret(trim(p_claim_code)) else null end,
    nullif(trim(p_build), ''),
    v_secret_hash,
    v_api_token_hash,
    v_api_token_hint,
    now(),
    now()
  )
  on conflict on constraint msv2_receivers_pkey do update
    set claim_code_hash = coalesce(public.msv2_receivers.claim_code_hash, excluded.claim_code_hash),
        build = coalesce(excluded.build, public.msv2_receivers.build),
        receiver_secret_hash = coalesce(public.msv2_receivers.receiver_secret_hash, excluded.receiver_secret_hash),
        receiver_api_token_hash = excluded.receiver_api_token_hash,
        receiver_api_token_hint = excluded.receiver_api_token_hint,
        cloud_registered_at = now(),
        last_seen_at = now();

  return query
    select p_receiver_id, v_api_token, v_api_token_hint;
end;
$$;

create or replace function public.msv2_issue_household_share(
  p_receiver_id text,
  p_claim_code text,
  p_household_name text default 'My Plants'
)
returns table (
  household_id uuid,
  receiver_id text,
  share_token text
)
language plpgsql
security definer
set search_path = public, extensions
as $$
declare
  v_receiver public.msv2_receivers%rowtype;
  v_household_id uuid;
  v_share_token text;
begin
  perform public.msv2_check_rate_limit('claim:' || p_receiver_id, 8, 3600);

  select *
    into v_receiver
  from public.msv2_receivers r
  where r.receiver_id = p_receiver_id;

  if not found then
    raise exception 'receiver not found';
  end if;

  if coalesce(v_receiver.claim_code_hash, '') <> coalesce(public.msv2_hash_secret(p_claim_code), '') then
    raise exception 'invalid claim code';
  end if;

  select c.household_id
    into v_household_id
  from public.msv2_receiver_claims c
  where c.receiver_id = p_receiver_id;

  if v_household_id is null then
    insert into public.msv2_households (name, created_by)
    values (coalesce(nullif(p_household_name, ''), 'My Plants'), null)
    returning msv2_households.household_id into v_household_id;

    insert into public.msv2_receiver_claims (receiver_id, household_id, claimed_by)
    values (p_receiver_id, v_household_id, null);
  end if;

  v_share_token := encode(gen_random_bytes(32), 'hex');

  insert into public.msv2_household_share_tokens (token_hash, household_id)
  values (public.msv2_hash_secret(v_share_token), v_household_id);

  return query
    select v_household_id, p_receiver_id, v_share_token;
end;
$$;

-- Real revoke path: msv2_household_share_tokens.revoked_at existed since 009 but nothing ever set it.
create or replace function public.msv2_revoke_household_share(
  p_share_token text
)
returns void
language plpgsql
security definer
set search_path = public, extensions
as $$
begin
  update public.msv2_household_share_tokens
    set revoked_at = now()
  where token_hash = public.msv2_hash_secret(p_share_token)
    and revoked_at is null;
end;
$$;

grant execute on function public.msv2_check_rate_limit(text, integer, integer) to service_role;
grant execute on function public.msv2_revoke_household_share(text) to service_role;
