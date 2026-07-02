create or replace function public.msv2_hash_secret(
  p_value text
)
returns text
language sql
immutable
strict
set search_path = public, extensions
as $$
  select encode(digest(convert_to(p_value, 'UTF8'), 'sha256'::text), 'hex');
$$;

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

  if found
     and v_existing.receiver_secret_hash is not null
     and v_existing.receiver_secret_hash <> v_secret_hash then
    raise exception 'receiver secret mismatch';
  end if;

  v_api_token := encode(gen_random_bytes(32), 'hex');
  v_api_token_hash := public.msv2_hash_secret(v_api_token);
  v_api_token_hint := right(v_api_token, 6);

  insert into public.msv2_receivers (
    receiver_id,
    claim_code,
    build,
    receiver_secret_hash,
    receiver_api_token_hash,
    receiver_api_token_hint,
    cloud_registered_at,
    last_seen_at
  )
  values (
    p_receiver_id,
    nullif(trim(p_claim_code), ''),
    nullif(trim(p_build), ''),
    v_secret_hash,
    v_api_token_hash,
    v_api_token_hint,
    now(),
    now()
  )
  on conflict on constraint msv2_receivers_pkey do update
    set claim_code = coalesce(excluded.claim_code, public.msv2_receivers.claim_code),
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

grant execute on function public.msv2_hash_secret(text) to anon, authenticated, service_role;
grant execute on function public.msv2_register_receiver(text, text, text, text) to anon, authenticated, service_role;
