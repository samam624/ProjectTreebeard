-- Lets a browser clear a receiver's sensor history directly via a household share
-- token, same auth pattern as msv2_read_share_sensor_history (015). Needed because
-- everything logged before today's oscillator-sensing fix (see
-- docs/MOISTURE_SENSOR_OSCILLATOR_REPORT.md) is garbage from the old one-shot RC
-- timing approach and on a completely different numeric scale - keeping it around only
-- pollutes future History tab charts with meaningless data.

create or replace function public.msv2_clear_share_sensor_history(
  p_share_token text,
  p_receiver_id text
)
returns bigint
language plpgsql
security definer
set search_path = public, extensions
as $$
declare
  v_household_id uuid;
  v_deleted bigint;
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

  delete from public.msv2_sensor_history h
  where h.receiver_id = p_receiver_id;

  get diagnostics v_deleted = row_count;
  return v_deleted;
end;
$$;

revoke execute on function public.msv2_clear_share_sensor_history(text, text) from public;
grant execute on function public.msv2_clear_share_sensor_history(text, text) to anon, authenticated, service_role;
