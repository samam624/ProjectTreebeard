-- Store one receiver queue batch in a single PostgREST/DB round trip. Calling the
-- single-reading RPC repeatedly from the Edge Function was correct but needlessly slow
-- for fast bench transmitters; this wrapper keeps the same scale-transition and
-- idempotency behavior inside one database transaction.

create or replace function public.msv2_store_sensor_reading_batch(
  p_receiver_id text,
  p_readings jsonb
)
returns integer
language plpgsql
security definer
set search_path = public
as $$
declare
  v_item jsonb;
  v_accepted integer := 0;
begin
  if jsonb_typeof(p_readings) <> 'array' then
    raise exception 'p_readings must be a JSON array';
  end if;
  if jsonb_array_length(p_readings) > 128 then
    raise exception 'history batch exceeds 128 rows';
  end if;

  for v_item in select value from jsonb_array_elements(p_readings)
  loop
    perform public.msv2_store_sensor_reading(
      p_receiver_id,
      (v_item->>'sensorId')::integer,
      nullif(v_item->>'wake', '')::integer,
      nullif(v_item->>'moisture', '')::integer,
      nullif(v_item->>'batteryPercent', '')::integer,
      nullif(v_item->>'rssi', '')::integer,
      nullif(v_item->>'ageMs', '')::integer,
      nullif(v_item->>'displayMoisture', '')::integer,
      coalesce(nullif(v_item->>'moistureScale', '')::integer, 1),
      nullif(v_item->>'rawMoistureKHz', '')::integer,
      nullif(v_item->>'rawFdcCounts', '')::bigint,
      nullif(v_item->>'observationId', ''),
      nullif(v_item->>'receivedAt', '')::timestamptz
    );
    v_accepted := v_accepted + 1;
  end loop;

  return v_accepted;
end;
$$;

revoke execute on function public.msv2_store_sensor_reading_batch(text, jsonb) from public, anon, authenticated;
grant execute on function public.msv2_store_sensor_reading_batch(text, jsonb) to service_role;
