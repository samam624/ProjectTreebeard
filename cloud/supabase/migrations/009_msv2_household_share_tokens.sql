create extension if not exists pgcrypto;

alter table public.msv2_households
  alter column created_by drop not null;

alter table public.msv2_receiver_claims
  alter column claimed_by drop not null;

create table if not exists public.msv2_household_share_tokens (
  token_hash text primary key,
  household_id uuid not null references public.msv2_households(household_id) on delete cascade,
  created_at timestamptz not null default now(),
  last_used_at timestamptz,
  revoked_at timestamptz
);

alter table public.msv2_household_share_tokens enable row level security;

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
  select *
    into v_receiver
  from public.msv2_receivers r
  where r.receiver_id = p_receiver_id;

  if not found then
    raise exception 'receiver not found';
  end if;

  if coalesce(v_receiver.claim_code, '') <> coalesce(p_claim_code, '') then
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

create or replace function public.msv2_read_share_latest_snapshots(
  p_share_token text
)
returns table (
  receiver_id text,
  snapshot jsonb,
  config_version integer,
  plant_count integer,
  received_at timestamptz,
  household_id uuid,
  household_name text
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

  update public.msv2_household_share_tokens t
    set last_used_at = now()
  where t.token_hash = public.msv2_hash_secret(p_share_token);

  return query
    select
      s.receiver_id,
      s.snapshot,
      s.config_version,
      s.plant_count,
      s.received_at,
      h.household_id,
      h.name as household_name
    from public.msv2_latest_snapshots s
    join public.msv2_receiver_claims c
      on c.receiver_id = s.receiver_id
    join public.msv2_households h
      on h.household_id = c.household_id
    where h.household_id = v_household_id
    order by s.received_at desc;
end;
$$;

grant execute on function public.msv2_issue_household_share(text, text, text) to anon, authenticated, service_role;
grant execute on function public.msv2_read_share_latest_snapshots(text) to anon, authenticated, service_role;
