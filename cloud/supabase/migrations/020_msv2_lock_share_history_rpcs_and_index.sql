-- Two fixes found in a full-repo hostile review (2026-08-13):
--
-- 1. msv2_read_share_sensor_history (015, redefined 018) and
-- msv2_clear_share_sensor_history (016) are the only RPCs in the project's history
-- granted execute to `anon, authenticated` in addition to `service_role` - every
-- sibling function (013/014/017/019, and the 011/012 hardening pass itself) locks
-- down to `service_role` only, since supabase/functions/plant-dashboard/index.ts is
-- the only real caller and always uses the service-role key. Granting anon/
-- authenticated lets anyone with the public anon key call these directly via
-- /rest/v1/rpc/<fn>, bypassing the edge function's method/CORS gating and any
-- future logging/rate-limiting added at that layer - not a cross-household IDOR
-- (both functions still verify token_hash + revoked_at is null + household-receiver
-- linkage internally), but an unnecessary widening of the attack surface against the
-- project's own "everything goes through edge functions" model. Bring both in line
-- with the rest of the schema.
--
-- 2. msv2_sensor_history's only index (013) is (receiver_id, sensor_id, received_at
-- desc), but every reader has paginated by `id` instead since 014/018 (id > p_after_id
-- / id < p_before_id, order by id asc/desc). As history grows across receivers/
-- sensors, that leaves every page query without an index matching its actual filter+
-- sort shape. The 018-established cursor direction itself is correct (newest-first,
-- no gaps/duplicates) - this only adds the missing index, no functional change.

revoke execute on function public.msv2_read_share_sensor_history(text, text, integer, timestamptz, bigint, integer) from anon, authenticated;
revoke execute on function public.msv2_clear_share_sensor_history(text, text) from anon, authenticated;

grant execute on function public.msv2_read_share_sensor_history(text, text, integer, timestamptz, bigint, integer) to service_role;
grant execute on function public.msv2_clear_share_sensor_history(text, text) to service_role;

create index if not exists msv2_sensor_history_receiver_sensor_id_idx
  on public.msv2_sensor_history(receiver_id, sensor_id, id desc);
