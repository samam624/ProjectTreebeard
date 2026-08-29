create table if not exists public.msv2_receivers (
  receiver_id text primary key,
  claim_code text,
  build text,
  first_seen_at timestamptz not null default now(),
  last_seen_at timestamptz not null default now(),
  last_ip inet
);

create table if not exists public.msv2_latest_snapshots (
  receiver_id text primary key references public.msv2_receivers(receiver_id) on delete cascade,
  snapshot jsonb not null,
  config_version integer,
  plant_count integer not null default 0,
  received_at timestamptz not null default now()
);

alter table public.msv2_receivers enable row level security;
alter table public.msv2_latest_snapshots enable row level security;

create index if not exists msv2_latest_snapshots_received_at_idx
  on public.msv2_latest_snapshots(received_at desc);

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
    claim_code,
    build,
    last_ip
  )
  values (
    p_receiver_id,
    p_claim_code,
    p_build,
    p_last_ip
  )
  on conflict (receiver_id) do update
    set claim_code = excluded.claim_code,
        build = excluded.build,
        last_seen_at = now(),
        last_ip = excluded.last_ip;
end;
$$;
