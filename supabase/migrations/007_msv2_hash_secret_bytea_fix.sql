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

grant execute on function public.msv2_hash_secret(text) to anon, authenticated, service_role;
