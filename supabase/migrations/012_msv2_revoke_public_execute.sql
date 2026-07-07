-- Migration 011 revoked EXECUTE from anon/authenticated on every RPC, but that did not actually
-- close the bypass: Postgres grants EXECUTE to the PUBLIC pseudo-role by default when a function
-- is created, and PUBLIC access applies to every role regardless of what's revoked from a specific
-- named role. Verified live: after 011, `msv2_check_rate_limit` (never granted to anon/authenticated,
-- only to service_role) was still callable via the public API key and returned 204. All of these
-- functions were originally CREATEd (not just replaced) at some point across migrations 001-011,
-- so they all picked up the default PUBLIC grant at creation time. This migration explicitly
-- revokes it.

revoke execute on function public.msv2_touch_receiver(text, text, text, inet) from public;
revoke execute on function public.msv2_store_latest_snapshot(text, jsonb, integer, integer) from public;
revoke execute on function public.msv2_read_latest_snapshots(text, integer) from public;
revoke execute on function public.msv2_claim_receiver(uuid, text, text, text) from public;
revoke execute on function public.msv2_read_user_latest_snapshots(uuid) from public;
revoke execute on function public.msv2_hash_secret(text) from public;
revoke execute on function public.msv2_register_receiver(text, text, text, text) from public;
revoke execute on function public.msv2_verify_receiver_api_token(text, text) from public;
revoke execute on function public.msv2_issue_household_share(text, text, text) from public;
revoke execute on function public.msv2_read_share_latest_snapshots(text) from public;
revoke execute on function public.msv2_upsert_share_receiver_config(text, text, jsonb) from public;
revoke execute on function public.msv2_read_share_receiver_configs(text) from public;
revoke execute on function public.msv2_read_receiver_pending_config(text, integer) from public;
revoke execute on function public.msv2_check_rate_limit(text, integer, integer) from public;
revoke execute on function public.msv2_revoke_household_share(text) from public;

-- Re-assert the intended grants explicitly (belt and suspenders -- CREATE OR REPLACE FUNCTION does
-- not reset existing ACLs, but make the intended end state unambiguous here rather than relying on
-- what happened to already be granted by earlier migrations).
grant execute on function public.msv2_touch_receiver(text, text, text, inet) to service_role;
grant execute on function public.msv2_store_latest_snapshot(text, jsonb, integer, integer) to service_role;
grant execute on function public.msv2_read_latest_snapshots(text, integer) to service_role;
grant execute on function public.msv2_claim_receiver(uuid, text, text, text) to service_role;
grant execute on function public.msv2_read_user_latest_snapshots(uuid) to service_role;
grant execute on function public.msv2_hash_secret(text) to service_role;
grant execute on function public.msv2_register_receiver(text, text, text, text) to service_role;
grant execute on function public.msv2_verify_receiver_api_token(text, text) to service_role;
grant execute on function public.msv2_issue_household_share(text, text, text) to service_role;
grant execute on function public.msv2_read_share_latest_snapshots(text) to service_role;
grant execute on function public.msv2_upsert_share_receiver_config(text, text, jsonb) to service_role;
grant execute on function public.msv2_read_share_receiver_configs(text) to service_role;
grant execute on function public.msv2_read_receiver_pending_config(text, integer) to service_role;
grant execute on function public.msv2_check_rate_limit(text, integer, integer) to service_role;
grant execute on function public.msv2_revoke_household_share(text) to service_role;
