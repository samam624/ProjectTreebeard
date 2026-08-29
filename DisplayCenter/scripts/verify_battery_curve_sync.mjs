// The browser History-chart port (MobileAppPage.h) hand-transcribes the firmware's
// LiPo voltage->percent curve and IR-compensation taper constants (MSV2_ESP32_Display_
// Runtime.ino), with only a code comment ("keep in sync") enforcing agreement. This project
// retunes those numbers as a matter of course - this script makes a divergence a build-time
// failure instead of a silently wrong History chart.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const firmware = await readFile(
  join(repoRoot, "arduino", "MSV2_ESP32_Display_Runtime", "MSV2_ESP32_Display_Runtime.ino"),
  "utf8",
);
const mobileApp = await readFile(
  join(repoRoot, "arduino", "MSV2_ESP32_Display_Runtime", "MobileAppPage.h"),
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

function parseCppFloatConst(source, name) {
  const match = source.match(new RegExp(`const float ${name} = (-?\\d+(?:\\.\\d+)?)F?;`));
  assert.ok(match, `Could not find firmware constant ${name} - has it been renamed?`);
  return Number(match[1]);
}

function parseJsConst(source, name) {
  const match = source.match(new RegExp(`const ${name} = (-?\\d+(?:\\.\\d+)?);`));
  assert.ok(match, `Could not find embedded JS constant ${name} - has it been renamed?`);
  return Number(match[1]);
}

const constantPairs = [
  ["CHARGING_IR_COMPENSATION_MV", "BATTERY_CHARGE_IR_COMPENSATION_MV"],
  ["CHARGE_TAPER_START_MV", "BATTERY_CHARGE_TAPER_START_MV"],
  ["CHARGE_TAPER_END_MV", "BATTERY_CHARGE_TAPER_END_MV"],
];
for (const [cppName, jsName] of constantPairs) {
  const cppValue = parseCppFloatConst(firmware, cppName);
  const jsValue = parseJsConst(mobileApp, jsName);
  assert.equal(
    jsValue,
    cppValue,
    `${jsName} (embedded JS, ${jsValue}) must match ${cppName} (firmware, ${cppValue}).`,
  );
}

console.log(
  `PASS battery curve (${cppCurve.length} points) and IR-compensation constants match between firmware and the embedded browser port`,
);
