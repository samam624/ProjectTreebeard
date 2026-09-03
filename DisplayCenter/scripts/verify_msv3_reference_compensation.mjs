import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const displayCenterRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const mobileApp = (
  await readFile(
    join(displayCenterRoot, "arduino", "Treebeard_ESP32_Display_Runtime", "MobileAppPage.h"),
    "utf8",
  )
).replace(/\r\n/g, "\n");

const helperSource = mobileApp.match(
  /function msv3ReferenceExcessPercent\(row\) \{[\s\S]*?\n    \}/,
)?.[0];
assert.ok(helperSource, "Could not extract the embedded MSV3 reference-compensation helper.");

const context = {};
vm.createContext(context);
vm.runInContext(`${helperSource}\nthis.compensate = msv3ReferenceExcessPercent;`, context);

const latest = context.compensate({
  raw_fdc_counts: 18_474_883,
  bare_tank_fdc_counts: 22_635_479,
});
assert.ok(
  Math.abs(latest - 50.112198) < 0.00001,
  "The screenshot's latest pair must produce about 50.1122% capacitance above bare.",
);

const original = context.compensate({ raw_fdc_counts: 18_000_000, bare_tank_fdc_counts: 22_000_000 });
const commonModeScaled = context.compensate({ raw_fdc_counts: 36_000_000, bare_tank_fdc_counts: 44_000_000 });
assert.equal(
  commonModeScaled,
  original,
  "Multiplying both FDC counts by the same common-mode factor must cancel exactly.",
);

const wetter = context.compensate({ raw_fdc_counts: 17_500_000, bare_tank_fdc_counts: 22_000_000 });
assert.ok(wetter > original, "Lower connected count at fixed bare count must read as wetter.");

for (const row of [
  null,
  {},
  { raw_fdc_counts: 0, bare_tank_fdc_counts: 22_000_000 },
  { raw_fdc_counts: 18_000_000, bare_tank_fdc_counts: 0x10000000 },
  { raw_fdc_counts: 18_000_000.5, bare_tank_fdc_counts: 22_000_000 },
]) {
  assert.equal(context.compensate(row), null, "Missing, zero, fractional, or out-of-range counts must be skipped.");
}

assert.match(
  mobileApp,
  /v: msv3ReferenceExcessPercent\(r\)/,
  "History rendering must derive compensation from the connected/bare values on each row.",
);
assert.match(
  mobileApp,
  /Bare-reference normalized FDC \(experimental, higher = wetter\)/,
  "The chart must stay visibly labeled experimental rather than claiming calibrated moisture.",
);

console.log("PASS MSV3 bare-reference normalization is paired, common-mode invariant, and water-direction preserving");
