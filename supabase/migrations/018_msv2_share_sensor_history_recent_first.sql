-- msv2_read_share_sensor_history (015) paginated forward from the START of the
-- requested window (order by id asc, id > p_after_id), capped by the caller's page
-- loop at HISTORY_MAX_PAGES * HISTORY_PAGE_SIZE = 12,000 rows total. That's fine for a
-- window with fewer rows than the cap, but a receiver posting every ~15-40s per sensor
-- can rack up several thousand rows/day - once a window's total exceeds 12,000, forward
-- pagination from the oldest end means the fetch runs out of page budget before ever
-- reaching "now". Confirmed live: 24h/6h views (under the cap) correctly showed
-- current data, while 3-day/7-day views (over the cap) silently froze at whatever
-- point exhausted the budget, further behind "now" the wider the window - no error,
-- just a chart that looked stale.
--
-- Fix: walk backward from the most recent row instead (order by id desc, id <
-- p_before_id), so the newest data is always included and the cap only ever truncates
-- the OLDER tail, never the current end. p_after_id is renamed p_before_id to match;
-- same position/type, so this replaces the existing function rather than overloading
-- it. Caller (plant-dashboard's shareHistoryApi + MobileAppPage.h's loadHistory) is
-- updated alongside this to page with beforeId and reverse the assembled pages once
-- at the end for chronological chart rendering.

drop function if exists public.msv2_read_share_sensor_history(text, text, integer, timestamptz, bigint, integer);

create or replace function public.msv2_read_share_sensor_history(
  p_share_token text,
  p_receiver_id text,
  p_sensor_id integer,
  p_since timestamptz default now() - interval '7 days',
  p_before_id bigint default 0,
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
      and (p_before_id = 0 or h.id < p_before_id)
    order by h.id desc
    limit p_limit;
end;
$$;

revoke execute on function public.msv2_read_share_sensor_history(text, text, integer, timestamptz, bigint, integer) from public;
grant execute on function public.msv2_read_share_sensor_history(text, text, integer, timestamptz, bigint, integer) to anon, authenticated, service_role;
