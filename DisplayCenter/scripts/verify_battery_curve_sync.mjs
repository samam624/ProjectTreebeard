// The browser History-chart port (MobileAppPage.h) hand-transcribes the firmware's
// LiPo voltage->percent curve (Treebeard_ESP32_Display_Runtime.ino), with only a code comment
// ("keep in sync") enforcing agreement. This script makes a divergence a build-time failure
// instead of a silently wrong History chart. Battery percentage intentionally uses measured
// voltage directly; raw charging state remains diagnostic-only.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const firmware = await readFile(
  join(repoRoot, "arduino", "Treebeard_ESP32_Display_Runtime", "Treebeard_ESP32_Display_Runtime.ino"),
  "utf8",
);
const mobileApp = await readFile(
  join(repoRoot, "arduino", "Treebeard_ESP32_Display_Runtime", "MobileAppPage.h"),
  "utf8",
);

function parseCppCurve(source) {
  const match = source.match(/const BatteryCurvePoint BATTERY_CURVE\[\] = \{([\s\S]*?)\};/);
  assert.ok(match, "Could not find firmware's BATTERY_CURVE[] - has it been renamed?");
  const points = [...match[1].matchAll(/\{\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*\}/g)].map(
    ([, mv, pct]) => ({ mv: Number(mv), pct: Number(pct) }),
  );
  assert.ok(points.length > 0, "Firmware BATTERY_CURVE[] parsed to zero points.");
  return points;
}

function parseJsCurve(source) {
  const match = source.match(/const BATTERY_CURVE = \[([\s\S]*?)\];/);
  assert.ok(match, "Could not find the embedded JS BATTERY_CURVE - has it been renamed?");
  const points = [...match[1].matchAll(/\{\s*mv:\s*(-?\d+(?:\.\d+)?)\s*,\s*pct:\s*(-?\d+(?:\.\d+)?)\s*\}/g)].map(
    ([, mv, pct]) => ({ mv: Number(mv), pct: Number(pct) }),
  );
  assert.ok(points.length > 0, "Embedded JS BATTERY_CURVE parsed to zero points.");
  return points;
}

const cppCurve = parseCppCurve(firmware);
const jsCurve = parseJsCurve(mobileApp);
assert.deepEqual(
  jsCurve,
  cppCurve,
  "MobileAppPage.h's BATTERY_CURVE has drifted from firmware's BATTERY_CURVE[] - the History " +
    "chart's battery smoothing would silently disagree with what the receiver actually displays.",
);

for (const removedSymbol of [
  "CHARGING_IR_COMPENSATION_MV",
  "CHARGE_TAPER_START_MV",
  "CHARGE_TAPER_END_MV",
  "BATTERY_CHARGE_IR_COMPENSATION_MV",
  "batteryChargeCompensationMv",
]) {
  assert.ok(
    !firmware.includes(removedSymbol) && !mobileApp.includes(removedSymbol),
    `${removedSymbol} must remain removed; battery percentage uses measured voltage directly.`,
  );
}

const rawMillivoltHelper = mobileApp.match(
  /function rawBatteryMillivoltSeries\(rows\) \{[\s\S]*?\n    \}/,
)?.[0];
assert.ok(rawMillivoltHelper, "Could not extract the raw battery-millivolt History helper.");

const context = {};
vm.createContext(context);
vm.runInContext(`${rawMillivoltHelper}\nthis.rawMv = rawBatteryMillivoltSeries;`, context);
assert.deepEqual(
  Array.from(context.rawMv([
    { t: 1, rawBatteryMv: 4150 },
    { t: 2, rawBatteryMv: null },
    { t: 3, rawBatteryMv: 4299 },
  ]), (point) => ({ t: point.t, v: point.v })),
  [{ t: 1, v: 4150 }, { t: 3, v: 4299 }],
  "The mV chart must preserve stored ADC values exactly and skip rows without raw millivolts.",
);
assert.match(mobileApp, /id="historyBatteryPercentButton"/, "Battery History must expose a percent-unit button.");
assert.match(mobileApp, /id="historyBatteryMvButton"/, "Battery History must expose a raw-mV unit button.");
assert.match(
  mobileApp,
  /effectiveBatteryUnit === "mv" \? batteryMillivoltPoints : batteryPercentPoints/,
  "Battery History must switch the plotted series rather than only relabeling the axis.",
);

console.log(
  `PASS battery curve (${cppCurve.length} points), charging compensation removal, and raw-mV History toggle`,
);
