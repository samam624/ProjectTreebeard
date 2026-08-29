create or replace function public.msv2_read_latest_snapshots(
  p_receiver_id text default null,
  p_limit integer default 20
)
returns table (
  receiver_id text,
  snapshot jsonb,
  config_version integer,
  plant_count integer,
  received_at timestamptz
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
    s.received_at
  from public.msv2_latest_snapshots s
  where p_receiver_id is null
     or s.receiver_id = p_receiver_id
  order by s.received_at desc
  limit greatest(1, least(coalesce(p_limit, 20), 50));
$$;

grant execute on function public.msv2_read_latest_snapshots(text, integer) to anon, authenticated, service_role;
