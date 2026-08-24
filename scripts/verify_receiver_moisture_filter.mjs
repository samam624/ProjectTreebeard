import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
// Normalize CRLF -> LF: this Windows checkout's working-tree files are CRLF (git core.autocrlf),
// and several regexes below rely on literal "\n\n" appearing back-to-back with nothing in
// between - a stray \r there breaks the match. Normalizing once here, rather than teaching
// every regex to tolerate \r, keeps this file's other patterns simple and correct regardless
// of which line-ending convention a given checkout renders.
const firmware = (
  await readFile(join(repoRoot, "arduino", "MSV2_ESP32_Display_Runtime", "MSV2_ESP32_Display_Runtime.ino"), "utf8")
).replace(/\r\n/g, "\n");
const mobileApp = (
  await readFile(join(repoRoot, "arduino", "MSV2_ESP32_Display_Runtime", "MobileAppPage.h"), "utf8")
).replace(/\r\n/g, "\n");

const scriptBlocks = [...mobileApp.matchAll(/<script>([\s\S]*?)<\/script>/g)];
assert.equal(scriptBlocks.length, 2, "MobileAppPage must contain the vendored QR and application script blocks.");
scriptBlocks.forEach((match, index) => {
  assert.doesNotThrow(
    () => new Function(match[1]),
    `Embedded MobileAppPage script ${index + 1} must remain valid JavaScript.`,
  );
});

assert.match(
  firmware,
  /moistureStalledWakes \+ 1U >= MOISTURE_MAX_STALLED_WAKES/,
  "Firmware must release the dry-side deadband on the configured qualifying packet, not one packet late.",
);
assert.match(
  firmware,
  /else if \(measuredMoisture == reading\.moisture\)[\s\S]*?moistureStalledWakes = 0;[\s\S]*?Preserve accumulated dry-side evidence/,
  "Ordinary upward noise must not erase accumulated dry-side evidence.",
);
assert.match(
  firmware,
  /const uint16_t presentationSpan = moisturePresentationSpan\(sensorId, moisture\);[\s\S]*?portENTER_CRITICAL\(&sensorsMux\);[\s\S]*?smoothMoistureForDisplay\(sensorId, moisture, presentationSpan\)/,
  "Calibration must be snapshotted before sensorsMux so the packet path does not nest state locks.",
);
assert.match(
  firmware,
  /portENTER_CRITICAL\(&sensorsMux\);[\s\S]*?if \(reading\.seen && wake == reading\.wake\)[\s\S]*?calibrateRawBattery\(sensorId, \(float\)rawBatteryMv, rawCharging\)[\s\S]*?smoothMoistureForDisplay\(sensorId, moisture, presentationSpan\)[\s\S]*?publishedReading = reading;[\s\S]*?portEXIT_CRITICAL\(&sensorsMux\);/,
  "Duplicate detection, stateful filters, record publication, and queue snapshot must share one sensors lock.",
);
assert.doesNotMatch(
  firmware.match(/void onResult\([\s\S]*?portENTER_CRITICAL\(&sensorsMux\);/)?.[0] ?? "",
  /calibrateRawBattery\(/,
  "Duplicate BLE frames must be rejected before stateful battery calibration.",
);
assert.doesNotMatch(
  mobileApp,
  /Defaults match the receiver firmware's own filter exactly/,
  "The window-local browser simulation must not claim exact reconstruction.",
);

const spanSource = mobileApp.match(/function moisturePresentationSpan\([\s\S]*?\n    }\n\n    \/\/ Faithful/)?.[0]
  .replace(/\n\n    \/\/ Faithful[\s\S]*$/, "");
const filterSource = mobileApp.match(/function smoothMoistureSeries\([\s\S]*?\n    }\n\n    \/\/ Battery voltage/)?.[0]
  .replace(/\n\n    \/\/ Battery voltage[\s\S]*$/, "");
assert.ok(spanSource && filterSource, "Could not extract the embedded browser moisture filter.");

const context = {};
vm.createContext(context);
vm.runInContext(`${spanSource}\n${filterSource}\nthis.runFilter = smoothMoistureSeries; this.span = moisturePresentationSpan;`, context);

assert.equal(context.span(40_000, 10_000), 10_000, "A ready learned span must override the fallback measured/5 span.");

const rawPoints = [{ t: 0, raw: 1000 }, { t: 1, raw: 1000 }];
for (let i = 0; i < 20; i += 1) {
  rawPoints.push({ t: 2 + i * 2, raw: 999 });
  rawPoints.push({ t: 3 + i * 2, raw: 1001 });
}
const output = context.runFilter(rawPoints, {
  learnedSpan: null,
  noiseDeadbandMult: 3,
  noiseWateringMult: 4,
  wateringSpanPct: 12,
  maxStepSpanPct: 5,
  confirmPackets: 2,
  stalledMaxWakes: 20,
  wetStalledMaxWakes: 0,
});
assert.ok(
  output.some((point) => point.v < 1000),
  "Alternating sub-deadband upward noise must not prevent convergence after 20 qualifying dry-side packets.",
);

console.log("PASS receiver/browser moisture filters preserve dry-side evidence across symmetric noise");
