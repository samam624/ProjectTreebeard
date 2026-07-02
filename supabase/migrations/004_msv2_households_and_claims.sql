create extension if not exists pgcrypto;

create table if not exists public.msv2_households (
  household_id uuid primary key default gen_random_uuid(),
  name text not null,
  created_by uuid not null references auth.users(id) on delete cascade,
  created_at timestamptz not null default now()
);

create table if not exists public.msv2_household_members (
  household_id uuid not null references public.msv2_households(household_id) on delete cascade,
  user_id uuid not null references auth.users(id) on delete cascade,
  role text not null default 'owner',
  created_at timestamptz not null default now(),
  primary key (household_id, user_id)
);

create table if not exists public.msv2_receiver_claims (
  receiver_id text primary key references public.msv2_receivers(receiver_id) on delete cascade,
  household_id uuid not null references public.msv2_households(household_id) on delete cascade,
  claimed_by uuid not null references auth.users(id) on delete cascade,
  claimed_at timestamptz not null default now()
);

alter table public.msv2_households enable row level security;
alter table public.msv2_household_members enable row level security;
alter table public.msv2_receiver_claims enable row level security;

create policy "members can read households"
  on public.msv2_households
  for select
  using (
    exists (
      select 1 from public.msv2_household_members m
      where m.household_id = msv2_households.household_id
        and m.user_id = auth.uid()
    )
  );

create policy "members can read household members"
  on public.msv2_household_members
  for select
  using (user_id = auth.uid());

create policy "members can read receiver claims"
  on public.msv2_receiver_claims
  for select
  using (
    exists (
      select 1 from public.msv2_household_members m
      where m.household_id = msv2_receiver_claims.household_id
        and m.user_id = auth.uid()
    )
  );

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
  select *
    into v_receiver
  from public.msv2_receivers
  where public.msv2_receivers.receiver_id = p_receiver_id;

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

create or replace function public.msv2_read_user_latest_snapshots(
  p_user_id uuid
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
language sql
security definer
set search_path = public
as $$
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
  join public.msv2_household_members m
    on m.household_id = h.household_id
  where m.user_id = p_user_id
  order by s.received_at desc;
$$;

grant execute on function public.msv2_claim_receiver(uuid, text, text, text) to anon, authenticated, service_role;
grant execute on function public.msv2_read_user_latest_snapshots(uuid) to anon, authenticated, service_role;
