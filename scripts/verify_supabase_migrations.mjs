import assert from "node:assert/strict";
import { readdir, readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const migrationRoot = join(repoRoot, "supabase", "migrations");
const migrations = (await readdir(migrationRoot))
  .filter((name) => /^\d{3}_.+\.sql$/.test(name))
  .sort();

assert.ok(migrations.length > 0, "No Supabase migrations found.");
const numbers = migrations.map((name) => Number(name.slice(0, 3)));
for (let index = 0; index < numbers.length; index += 1) {
  assert.equal(numbers[index], index + 1, `Migration sequence has a gap or duplicate near ${migrations[index]}.`);
}

const latest = String(numbers.at(-1)).padStart(3, "0");
const setup = await readFile(join(repoRoot, "docs", "SUPABASE_SETUP.md"), "utf8");
assert.ok(
  setup.includes(`currently \`${latest}\``) && setup.includes("highest-numbered migration present"),
  `Supabase setup must direct fresh installs through latest migration ${latest}.`,
);

for (const name of ["019_msv2_receiver_display_settings.sql", "033_msv2_display_settings_upsert_conflict_fix.sql"]) {
  const sql = await readFile(join(migrationRoot, name), "utf8");
  assert.ok(
    sql.includes("on conflict on constraint msv2_receiver_display_settings_pkey"),
    `${name} must use the unambiguous named display-settings primary-key constraint.`,
  );
  assert.ok(
    !sql.includes("on conflict (receiver_id) do update"),
    `${name} reintroduced the ambiguous PL/pgSQL conflict target.`,
  );
}

console.log(`PASS ${migrations.length} contiguous Supabase migrations; setup includes latest ${latest}`);
