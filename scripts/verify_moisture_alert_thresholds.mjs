import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");

const firmware = await readFile(join(repoRoot, "arduino", "MSV2_ESP32_Display_Runtime", "MSV2_ESP32_Display_Runtime.ino"), "utf8");
const localApp = await readFile(join(repoRoot, "arduino", "MSV2_ESP32_Display_Runtime", "MobileAppPage.h"), "utf8");
const dashboardHtml = await readFile(join(repoRoot, "web", "plant-dashboard", "index.html"), "utf8");
const serviceWorker = await readFile(join(repoRoot, "web", "plant-dashboard", "sw.js"), "utf8");
const appMatch = dashboardHtml.match(/src="assets\/(app\.v(\d+)\.js)"/);
assert.ok(appMatch, "Dashboard HTML must reference a versioned app.vN.js bundle.");
const appAsset = appMatch[1];
const appVersion = appMatch[2];
const cloudApp = await readFile(join(repoRoot, "web", "plant-dashboard", "assets", appAsset), "utf8");

// 2026-08-23: 0 is the watering-line inflection point - literally the driest a sensor has
// ever recorded (moisturePercent()'s own zero), NOT a padded-above-dry cue. 0-100 (the
// entire learned dry-to-wet range) is normal, unchanged from how it has always displayed -
// moisturePercent() is unclamped below zero only, so a reading drier than ever recorded
// counts down as a negative number instead of pinning at 0. Below 0: a quiet yellow warning,
// then red/flashing at MOISTURE_DANGER_PERCENT. Separately, MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT
// is a display-LAYOUT constant (not an alert threshold): the bar's dotted marker and the
// boundary between its "0-100" region and its open-ended negative region both sit at this
// fraction of the bar's width, since a bar can't render negative width - the visible number
// stays unclamped even once the bar itself has gone fully empty ("goes off the end of the
// bar"). This asserts the three surfaces AGREE with each other on both the alert thresholds
// and the bar-layout constant, rather than pinning all three to hardcoded numbers here - a
// deliberate future change only has to update the source files and stay consistent, not also
// update this test's own magic numbers.
function extractConst(source, pattern, label) {
  const match = source.match(pattern);
  assert.ok(match, `${label} - has it been renamed?`);
  return Number(match[1]);
}

const firmwareWaterLine = extractConst(firmware, /const int16_t MOISTURE_WATER_LINE_PERCENT = (-?\d+);/, "Firmware must define MOISTURE_WATER_LINE_PERCENT");
const firmwareDanger = extractConst(firmware, /const int16_t MOISTURE_DANGER_PERCENT = (-?\d+);/, "Firmware must define MOISTURE_DANGER_PERCENT");
const firmwareBarNegWidth = extractConst(firmware, /const uint8_t MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT = (\d+);/, "Firmware must define MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT");

for (const [label, source] of [["Local app", localApp], ["Cloud app", cloudApp]]) {
  const waterLine = extractConst(source, /const MOISTURE_WATER_LINE_PCT = (-?\d+);/, `${label} must define MOISTURE_WATER_LINE_PCT`);
  const danger = extractConst(source, /const MOISTURE_DANGER_PCT = (-?\d+);/, `${label} must define MOISTURE_DANGER_PCT`);
  const barNegWidth = extractConst(source, /const MOISTURE_BAR_NEGATIVE_WIDTH_PCT = (\d+);/, `${label} must define MOISTURE_BAR_NEGATIVE_WIDTH_PCT`);
  assert.equal(waterLine, firmwareWaterLine, `${label}'s watering line (${waterLine}) must match firmware's (${firmwareWaterLine}).`);
  assert.equal(danger, firmwareDanger, `${label}'s danger threshold (${danger}) must match firmware's (${firmwareDanger}).`);
  assert.equal(barNegWidth, firmwareBarNegWidth, `${label}'s bar layout constant (${barNegWidth}) must match firmware's (${firmwareBarNegWidth}).`);
}

assert.ok(firmwareDanger < firmwareWaterLine, `Firmware's danger threshold (${firmwareDanger}) must be strictly below its watering line (${firmwareWaterLine}).`);
assert.equal(firmwareWaterLine, 0, "The watering line is defined as the literal dry-bound zero point - a nonzero value here means the model has drifted from '0-100 stays exactly as it always was'.");

// moisturePercent()/pct()/percent() must all be unclamped below zero (only capped at 100 on
// top) - re-clamping to 0 anywhere would silently collapse the whole negative extension this
// model exists for.
assert.doesNotMatch(
  firmware.match(/int16_t moisturePercent\([\s\S]*?\n}/)?.[0] ?? "",
  /clampInt\([^)]*,\s*0,\s*100\)/,
  "Firmware moisturePercent() must not clamp its lower bound to 0 - it needs to go negative.",
);
assert.doesNotMatch(
  localApp.match(/const pct = \(value, dry, wet\) => \{[\s\S]*?\n {4}\};/)?.[0] ?? "",
  /Math\.max\(0,/,
  "Local app's pct() must not clamp its lower bound to 0 - it needs to go negative.",
);
assert.doesNotMatch(
  cloudApp.match(/function percent\(value, dry, wet\) \{[\s\S]*?\n\}/)?.[0] ?? "",
  /Math\.max\(0,/,
  "Cloud app's percent() must not clamp its lower bound to 0 - it needs to go negative.",
);

assert.match(
  firmware,
  /uint16_t moistureHealthColor[\s\S]*?if \(percent <= MOISTURE_DANGER_PERCENT\)[\s\S]*?return dangerColor;[\s\S]*?if \(percent < MOISTURE_WATER_LINE_PERCENT\)[\s\S]*?return warningColor;/,
  "Firmware moisture color must go danger at-or-below the danger threshold and warning strictly below the watering line (0-100 itself must stay normal).",
);
assert.match(
  firmware,
  /bool slotNeedsBlink[\s\S]*?return percent <= MOISTURE_DANGER_PERCENT;/,
  "Physical-display blinking and its shared buzzer trigger must activate at-or-below the danger threshold, not the watering line.",
);
assert.match(
  firmware,
  /uint16_t batteryHealthColor[\s\S]*?if \(percent < 25\)[\s\S]*?if \(percent < 50\)/,
  "Moisture threshold changes must not silently alter battery colors.",
);
assert.ok(
  localApp.includes('p <= MOISTURE_DANGER_PCT ? "danger" : p < MOISTURE_WATER_LINE_PCT ? "warn"') &&
    cloudApp.includes("if (pct <= MOISTURE_DANGER_PCT) return \"danger\";") &&
    cloudApp.includes("if (pct < MOISTURE_WATER_LINE_PCT) return \"warn\";"),
  "Local and cloud web apps must use the same three-tier thresholds (0-100 normal, strictly-negative-above-danger warn, at-or-below-danger red).",
);

// The three surfaces must actually flash/blink the danger state, not just color it red -
// "overdue" is meant to be visually distinct from "just yellow".
assert.match(firmware, /applyLowMoistureBlink/, "Firmware must still blink the danger state.");
assert.ok(
  localApp.includes("animation: moisture-flash") && dashboardHtml.includes("animation: moisture-flash"),
  "Both web clients must visually flash the danger state (a CSS animation on .fill.danger / .bar.danger span), not just color it red.",
);

assert.ok(
  serviceWorker.includes(`"msv2-cloud-app-v${appVersion}"`) &&
    serviceWorker.includes(`"/assets/${appAsset}"`),
  "Dashboard HTML and service-worker cache must reference the same cache-busted app version.",
);

console.log(
  `CHECK moisture alert thresholds agree across firmware and both web clients (water line ${firmwareWaterLine}, danger ${firmwareDanger}, bar negative-zone width ${firmwareBarNegWidth}%)`,
);
