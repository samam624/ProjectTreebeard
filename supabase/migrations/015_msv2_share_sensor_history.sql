-- Lets a browser read sensor history directly via a household share token, instead
-- of routing every paginated history page through the receiver's own blocking HTTP
-- proxy (handleHistoryProxy in the .ino, removed alongside this migration) - that
-- proxy shares the ESP32's single loop with display rendering, so looping dozens of
-- pages through it froze the physical display for the length of the whole fetch.
-- Mirrors msv2_upsert_share_receiver_config's token->household->membership check
-- (010_msv2_receiver_cloud_config.sql) rather than msv2_read_sensor_history's
-- receiver-token check (013/014), since this is reached directly from a browser.

create or replace function public.msv2_read_share_sensor_history(
  p_share_token text,
  p_receiver_id text,
  p_sensor_id integer,
  p_since timestamptz default now() - interval '7 days',
  p_after_id bigint default 0,
  p_limit integer default 500
)
returns table (
  id bigint,
  sensor_id integer,
  wake integer,
  moisture integer,
  battery_percent integer,
  rssi integer,
  age_ms integer,
  received_at timestamptz
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

  if not exists (
    select 1
    from public.msv2_receiver_claims c
    where c.household_id = v_household_id
      and c.receiver_id = p_receiver_id
  ) then
    raise exception 'receiver is not linked to this household';
  end if;

  return query
    select h.id, h.sensor_id, h.wake, h.moisture, h.battery_percent, h.rssi, h.age_ms, h.received_at
    from public.msv2_sensor_history h
    where h.receiver_id = p_receiver_id
      and h.sensor_id = p_sensor_id
      and h.received_at >= p_since
      and h.id > p_after_id
    order by h.id asc
    limit p_limit;
end;
$$;

revoke execute on function public.msv2_read_share_sensor_history(text, text, integer, timestamptz, bigint, integer) from public;
grant execute on function public.msv2_read_share_sensor_history(text, text, integer, timestamptz, bigint, integer) to anon, authenticated, service_role;
