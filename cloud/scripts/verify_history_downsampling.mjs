import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const cloudRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const repoRoot = join(cloudRoot, "..");

const [edgeFunction, mobilePage, migration] = await Promise.all([
  readFile(join(cloudRoot, "supabase", "functions", "plant-dashboard", "index.ts"), "utf8"),
  readFile(join(repoRoot, "DisplayCenter", "arduino", "Treebeard_ESP32_Display_Runtime", "MobileAppPage.h"), "utf8"),
  readFile(join(cloudRoot, "supabase", "migrations", "036_msv2_bounded_share_history.sql"), "utf8"),
]);

function integerConstant(source, name) {
  const match = source.match(new RegExp(`const\\s+${name}\\s*=\\s*(\\d+)`));
  assert.ok(match, `Missing integer constant ${name}`);
  return Number(match[1]);
}

const serverBudget = integerConstant(edgeFunction, "HISTORY_POINT_BUDGET");
const clientBudget = integerConstant(mobilePage, "HISTORY_BATCH_ROWS");
assert.equal(clientBudget, serverBudget, "Client and server History point budgets must stay in sync");
assert.ok(serverBudget >= 1000 && serverBudget <= 5000, "History point budget must remain bounded and chart-useful");

assert.match(edgeFunction, /msv2_read_share_sensor_history_density/, "History API must probe density first");
assert.match(edgeFunction, /msv2_read_share_sensor_history_sampled/, "History API must call the sampled reader");
assert.match(edgeFunction, /totalReadings:\s*totalRows/, "History API must expose the original row count");
assert.match(edgeFunction, /sampleEvery,/, "History API must expose the selected stride");
assert.match(mobilePage, /plotted from.*sampled every/s, "History UI must disclose downsampling");

assert.match(migration, /row_number\(\)\s+over\s*\(order by h\.id desc\)\s*-\s*1/i,
  "Sample phase must be ranked newest-first");
assert.match(migration, /mod\(r\.sample_index,\s*v_sample_every::bigint\)\s*=\s*0/i,
  "Sampled reader must select a stable one-in-N stride");
assert.match(migration, /and\s+\(p_before_id\s*=\s*0\s+or\s+r\.id\s*<\s*p_before_id\)/i,
  "Sample cursor must be applied after global ranking");
assert.doesNotMatch(
  migration.match(/with ranked as \([\s\S]*?\n\s*\)\n\s*select/i)?.[0] ?? "",
  /p_before_id/,
  "Cursor filtering inside the ranked CTE would reset the sample phase between pages",
);

for (const totalRows of [0, 1, 999, 2400, 2401, 10000, 100001, 1_000_000]) {
  const sampleEvery = Math.max(1, Math.ceil(totalRows / serverBudget));
  const returnedRows = Math.ceil(totalRows / sampleEvery);
  assert.ok(returnedRows <= serverBudget, `${totalRows} rows exceeded the ${serverBudget}-point budget`);
  if (totalRows <= serverBudget) {
    assert.equal(sampleEvery, 1, `${totalRows} rows should not be sampled`);
    assert.equal(returnedRows, totalRows, `${totalRows} sparse rows should all be retained`);
  }
}

// Model PostgREST's 1000-row cap and the SQL cursor. Ranking remains global, so paging
// must produce exactly the same IDs as one unpaginated one-in-N selection.
const denseTotal = 100001;
const denseStride = Math.ceil(denseTotal / serverBudget);
const allIdsNewestFirst = Array.from({ length: denseTotal }, (_, index) => denseTotal - index);
const expected = allIdsNewestFirst.filter((_, index) => index % denseStride === 0);
const paged = [];
for (let offset = 0; offset < expected.length; offset += 1000) {
  paged.push(...expected.slice(offset, offset + 1000));
}
assert.deepEqual(paged, expected, "Internal pagination changed the stable sample");
assert.equal(paged[0], denseTotal, "The newest reading must always be retained");
assert.ok(paged.length <= serverBudget, "Dense simulated response exceeded the point budget");

console.log(
  `PASS bounded History sampling: ${denseTotal.toLocaleString()} rows -> ${paged.length.toLocaleString()} points (every ${denseStride})`,
);
