-- msv2_read_sensor_history (013) returns the whole requested window in one shot, which
-- was fine for a quick smoke test but breaks down for an overnight run: at a few thousand
-- rows the JSON response is large enough that the ESP32's http.getString() call (fixed
-- timeout, single buffered read) truncates it mid-object rather than erroring - confirmed
-- live, "Unexpected end of JSON input" on the receiver's History tab with the response
-- cut off mid-row. The real fix is pagination: the receiver/browser fetches bounded pages
-- (a few hundred rows each, always fast) and assembles the full range client-side instead
-- of asking the ESP32 to buffer one large response.

drop function if exists public.msv2_read_sensor_history(text, integer, timestamptz, integer);

create or replace function public.msv2_read_sensor_history(
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
language sql
security definer
set search_path = public
as $$
  select h.id, h.sensor_id, h.wake, h.moisture, h.battery_percent, h.rssi, h.age_ms, h.received_at
  from public.msv2_sensor_history h
  where h.receiver_id = p_receiver_id
    and h.sensor_id = p_sensor_id
    and h.received_at >= p_since
    and h.id > p_after_id
  order by h.id asc
  limit p_limit;
$$;

revoke execute on function public.msv2_read_sensor_history(text, integer, timestamptz, bigint, integer) from public;
grant execute on function public.msv2_read_sensor_history(text, integer, timestamptz, bigint, integer) to service_role;
