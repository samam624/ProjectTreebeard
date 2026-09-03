-- Mitigates (does not fully close - see note below) the receiver-bootstrap
-- registration race found in the 2026-08-13/14 full-repo review:
-- msv2_register_receiver's very first insert for a never-before-seen receiver_id is
-- accepted from whoever calls it first, no proof of physical possession required.
-- receiver_id is derived directly from the device's own MAC address
-- (ensureDeviceIdentity() in Treebeard_ESP32_Display_Runtime.ino: "MSV2" + hex(efuse MAC)),
-- which is passively observable by anyone in WiFi range of the device's own
-- MSV2-Display open setup AP before it ever reaches home WiFi - it is not a secret.
-- An attacker who front-runs the real device's own automatic post-WiFi-setup
-- bootstrap call can permanently lock it out ("receiver secret mismatch" forever
-- after) and post fabricated sensor data under its identity.
--
-- Why this migration does NOT add a "prove you know the claim code" check as a full
-- fix: claim_code_hash is itself established via the exact same unauthenticated
-- first-caller-wins insert as receiver_secret_hash, so it provides no independent
-- verification - a real attacker attempting this race would simply submit a
-- fabricated claim code alongside the fabricated secret in the same request, at zero
-- extra cost. A cryptographically sound fix needs a pre-shared secret established
-- before the device's first network exposure (i.e. manufacturing-time provisioning),
-- which would break this project's deliberate zero-touch onboarding design (see
-- docs/STATUS_HANDOFF.md "Important Preferences" - no manual per-device Supabase
-- setup). That tradeoff is a product decision, not made here.
--
-- What this migration does instead, on the axes actually improvable without that
-- tradeoff:
-- 1. Requires a non-empty claim code on first-time registration. Costs the real
--    device nothing (ensureDeviceIdentity() always generates one alongside the
--    secret, before either is ever sent anywhere), while rejecting careless/buggy
--    claim-code-less registration attempts outright and leaving an unambiguous
--    forensic signal (a rejected-with-no-claim-code attempt is never legitimate
--    firmware behavior).
-- 2. Adds a GLOBAL rate limit on brand-new-receiver creation (as opposed to the
--    existing 011 per-receiver_id limit, which only throttles repeated attempts
--    against ONE target and does nothing to slow a bulk/speculative squatting
--    campaign sweeping the ~48-bit MAC-derived id space hoping to catch real
--    devices' first-ever bootstrap calls before they happen). Bounds how much of
--    that space can be squatted per hour without limiting realistic legitimate
--    usage (this is a small-scale project - a handful of new receivers going live
--    in the same hour is already a generous ceiling).

create or replace function public.msv2_register_receiver(
  p_receiver_id text,
  p_receiver_secret text,
  p_claim_code text,
  p_build text default null
)
returns table (
  receiver_id text,
  api_token text,
  api_token_hint text
)
language plpgsql
security definer
set search_path = public, extensions
as $$
declare
  v_existing public.msv2_receivers%rowtype;
  v_secret_hash text;
  v_api_token text;
  v_api_token_hash text;
  v_api_token_hint text;
begin
  perform public.msv2_check_rate_limit('register:' || p_receiver_id, 8, 3600);

  if nullif(trim(p_receiver_id), '') is null then
    raise exception 'receiver id required';
  end if;

  if nullif(trim(p_receiver_secret), '') is null then
    raise exception 'receiver secret required';
  end if;

  v_secret_hash := public.msv2_hash_secret(p_receiver_secret);

  select *
    into v_existing
  from public.msv2_receivers r
  where r.receiver_id = p_receiver_id;

  if not found then
    if nullif(trim(p_claim_code), '') is null then
      raise exception 'claim code required for first-time registration';
    end if;

    perform public.msv2_check_rate_limit('register-new-global', 20, 3600);
  elsif v_existing.receiver_secret_hash is not null
        and v_existing.receiver_secret_hash <> v_secret_hash then
    raise exception 'receiver secret mismatch';
  end if;

  v_api_token := encode(gen_random_bytes(32), 'hex');
  v_api_token_hash := public.msv2_hash_secret(v_api_token);
  v_api_token_hint := right(v_api_token, 6);

  insert into public.msv2_receivers (
    receiver_id,
    claim_code_hash,
    build,
    receiver_secret_hash,
    receiver_api_token_hash,
    receiver_api_token_hint,
    cloud_registered_at,
    last_seen_at
  )
  values (
    p_receiver_id,
    case when nullif(trim(p_claim_code), '') is not null then public.msv2_hash_secret(trim(p_claim_code)) else null end,
    nullif(trim(p_build), ''),
    v_secret_hash,
    v_api_token_hash,
    v_api_token_hint,
    now(),
    now()
  )
  on conflict on constraint msv2_receivers_pkey do update
    set claim_code_hash = coalesce(public.msv2_receivers.claim_code_hash, excluded.claim_code_hash),
        build = coalesce(excluded.build, public.msv2_receivers.build),
        receiver_secret_hash = coalesce(public.msv2_receivers.receiver_secret_hash, excluded.receiver_secret_hash),
        receiver_api_token_hash = excluded.receiver_api_token_hash,
        receiver_api_token_hint = excluded.receiver_api_token_hint,
        cloud_registered_at = now(),
        last_seen_at = now();

  return query
    select p_receiver_id, v_api_token, v_api_token_hint;
end;
$$;

-- create-or-replace preserves the existing grants from migration 011/012
-- (service_role only) - no grant statements needed here.
