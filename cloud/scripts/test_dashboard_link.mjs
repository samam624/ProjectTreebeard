import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const html = readFileSync(new URL('../web/plant-dashboard/index.html', import.meta.url), 'utf8');
const asset = html.match(/src="assets\/(app\.v\d+\.js)"/)[1];
const source = readFileSync(new URL('../web/plant-dashboard/assets/' + asset, import.meta.url), 'utf8');
assert.equal(spawnSync(process.execPath, ['--check', fileURLToPath(new URL('../web/plant-dashboard/assets/' + asset, import.meta.url))]).status, 0);
async function run(query, status) {
  const nodes = new Map();
  const stored = new Map([['msv2.householdShareToken', 'household-A']]);
  const calls = [];
  const element = () => ({ value: '', addEventListener() {}, classList: { toggle() {} } });
  const getElementById = key => {
    if (!nodes.has(key)) nodes.set(key, element());
    return nodes.get(key);
  };
  const context = {
    URL, URLSearchParams, console,
    window: { location: { search: query, href: 'https://fixture.invalid/' + query }, addEventListener() {}, setInterval() {} },
    document: { getElementById, body: element(), cookie: '', addEventListener() {} },
    navigator: {},
    localStorage: { getItem: key => stored.get(key), setItem: (key, value) => stored.set(key, value), removeItem: key => stored.delete(key) },
    fetch: async url => {
      const route = url.split('/').pop();
      calls.push(route);
      return { ok: status === 200, status, json: async () => status === 200
        ? { ok: true, snapshots: [], share: { share_token: 'household-B' } }
        : { ok: false, error: 'fixture failure' } };
    },
  };
  vm.runInNewContext(source, context);
  await new Promise(resolve => setImmediate(resolve));
  return { stored, calls };
}
const paired = await run('?rid=B&code=123&connect=1', 200);
assert.equal(paired.calls[0], 'share-claim');
assert.equal(paired.stored.get('msv2.householdShareToken'), 'household-B');
assert.equal((await run('', 503)).stored.get('msv2.householdShareToken'), 'household-A');
assert.equal((await run('', 401)).stored.has('msv2.householdShareToken'), false);
console.log('PASS household switching and transient refresh recovery');
