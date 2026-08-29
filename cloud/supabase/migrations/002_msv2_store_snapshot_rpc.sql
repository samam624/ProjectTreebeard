create or replace function public.msv2_store_latest_snapshot(
  p_receiver_id text,
  p_snapshot jsonb,
  p_config_version integer,
  p_plant_count integer
)
returns void
language plpgsql
security definer
set search_path = public
as $$
begin
  insert into public.msv2_latest_snapshots (
    receiver_id,
    snapshot,
    config_version,
    plant_count,
    received_at
  )
  values (
    p_receiver_id,
    p_snapshot,
    p_config_version,
    p_plant_count,
    now()
  )
  on conflict (receiver_id) do update
    set snapshot = excluded.snapshot,
        config_version = excluded.config_version,
        plant_count = excluded.plant_count,
        received_at = now();
end;
$$;

grant execute on function public.msv2_store_latest_snapshot(text, jsonb, integer, integer) to anon, authenticated, service_role;
