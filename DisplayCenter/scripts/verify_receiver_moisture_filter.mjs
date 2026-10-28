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
  await readFile(join(repoRoot, "arduino", "Treebeard_ESP32_Display_Runtime", "Treebeard_ESP32_Display_Runtime.ino"), "utf8")
).replace(/\r\n/g, "\n");
const mobileApp = (
  await readFile(join(repoRoot, "arduino", "Treebeard_ESP32_Display_Runtime", "MobileAppPage.h"), "utf8")
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
  /constexpr uint8_t MOISTURE_CENTER_WINDOW = 5;/,
  "Firmware must robust-center five unique moisture packets.",
);
assert.match(
  firmware,
  /int32_t stepQ8 = \(targetQ8 - filteredQ8\) \/ MOISTURE_TRACKING_DIVISOR;/,
  "Firmware's normal moisture tracking must use the same signed update in both directions.",
);
assert.doesNotMatch(
  firmware,
  /moistureStalledWakes|moistureWateringPending|pendingDryDown/,
  "The asymmetric dry-side ratchet state must not survive the centered-filter replacement.",
);
assert.match(
  firmware,
  /const uint16_t presentationSpan = moisturePresentationSpan\(sensorId, moisture\);[\s\S]*?portENTER_CRITICAL\(&sensorsMux\);[\s\S]*?smoothMoistureForDisplay\([\s\S]*?sensorId, presentationInput, presentationSpan, msv4\)/,
  "Calibration must be snapshotted before sensorsMux so the packet path does not nest state locks.",
);
assert.match(
  firmware,
  /portENTER_CRITICAL\(&sensorsMux\);[\s\S]*?if \(reading\.seen && wake == reading\.wake\)[\s\S]*?calibrateRawBattery\(sensorId, \(float\)rawBatteryMv, msv4, wideRaw\)[\s\S]*?smoothMoistureForDisplay\([\s\S]*?sensorId, presentationInput, presentationSpan, msv4\)[\s\S]*?publishedReading = reading;[\s\S]*?portEXIT_CRITICAL\(&sensorsMux\);/,
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

const spanSource = mobileApp.match(/function moisturePresentationSpan\([\s\S]*?\n    }\n\n    function medianOfValues/)?.[0]
  .replace(/\n\n    function medianOfValues[\s\S]*$/, "");
const filterSource = mobileApp.match(/function medianOfValues\([\s\S]*?\n    }\n\n    \/\/ Battery voltage/)?.[0]
  .replace(/\n\n    \/\/ Battery voltage[\s\S]*$/, "");
assert.ok(spanSource && filterSource, "Could not extract the embedded browser moisture filter.");

const context = {};
vm.createContext(context);
vm.runInContext(`${spanSource}\n${filterSource}\nthis.runFilter = smoothMoistureSeries; this.span = moisturePresentationSpan;`, context);

assert.equal(context.span(40_000, 10_000), 10_000, "A ready learned span must override the fallback measured/5 span.");

const defaults = {
  learnedSpan: 360,
  centerWindowPackets: 5,
  moistureAlpha: 0.125,
  noiseWateringMult: 4,
  wateringSpanPct: 12,
  maxStepSpanPct: 5,
  confirmPackets: 2,
};
const asPoints = (values) => values.map((raw, t) => ({ t, raw }));

// Reproduce Alocasia GP's observed shape: a stable center around 896-903 with frequent
// excursions on both sides and occasional much lower packets. The former drying ratchet
// reached 879 while the live raw median remained about 900; centered smoothing must not.
const alocasiaPattern = [
  919, 900, 896, 898, 896, 903, 896, 893, 905, 898,
  898, 921, 892, 896, 896, 901, 896, 908, 893, 896,
  892, 885, 896, 896, 903, 914, 905, 886, 893, 874,
];
const stationaryRaw = [900, 900, 900, 900, 900];
for (let i = 0; i < 30; i += 1) {
  stationaryRaw.push(...alocasiaPattern);
}
const stationaryOutput = context.runFilter(asPoints(stationaryRaw), defaults);
const stationaryTail = stationaryOutput.slice(-alocasiaPattern.length * 10);
assert.ok(
  Math.min(...stationaryTail.map((point) => point.v)) >= 894,
  "Stationary Alocasia-like noise must not ratchet the displayed value toward its lower envelope.",
);
assert.ok(
  Math.abs(stationaryOutput.at(-1).v - 898) <= 3,
  "The displayed value must remain near the stationary signal's robust center.",
);

const alternating = [1000, 1000, 1000, 1000, 1000];
for (let i = 0; i < 200; i += 1) alternating.push(i % 2 ? 1001 : 999);
const alternatingOutput = context.runFilter(asPoints(alternating), { ...defaults, learnedSpan: 200 });
assert.ok(
  Math.abs(alternatingOutput.at(-1).v - 1000) <= 1,
  "Symmetric noise must produce an unbiased centered result.",
);

const isolatedSpikes = [
  ...Array(12).fill(1000),
  1600, 1600,
  ...Array(12).fill(1000),
];
const spikeOutput = context.runFilter(asPoints(isolatedSpikes), { ...defaults, learnedSpan: 300 });
assert.equal(
  Math.max(...spikeOutput.map((point) => point.v)),
  1000,
  "Two isolated high packets must be rejected by the five-packet robust center.",
);

const watering = [...Array(12).fill(1000), ...Array(8).fill(1300)];
const wateringOutput = context.runFilter(asPoints(watering), { ...defaults, learnedSpan: 300 });
assert.equal(
  wateringOutput[15].v,
  1300,
  "A sustained watering step must snap to its robust center on the fourth high packet.",
);

const dryDown = [...Array(12).fill(1000), ...Array(50).fill(800)];
const dryOutput = context.runFilter(asPoints(dryDown), { ...defaults, learnedSpan: 300 });
assert.ok(
  dryOutput.at(-1).v <= 802 && dryOutput.at(-1).v >= 800,
  "A sustained dry-down must converge smoothly to its new center.",
);

// ---- MSV4 drying hold (2026-10-28) ----
assert.match(
  firmware,
  /const uint16_t trackedMoisture = holdBetweenWaterings\s*\? \(uint16_t\)\(\(reading\.moistureFilteredQ8 \+ 128U\) >> 8\)\s*: reading\.moisture;[\s\S]*?bool wateringRise = center > trackedMoisture/,
  "With the hold, firmware watering detection must compare against the tracker, not the held value.",
);
const MIN = 60_000;
const timed = (values, hold = true) => values.map((raw, i) => ({ t: i * MIN, raw, hold }));
const msv4Params = { ...defaults, learnedSpan: 8000 };

// Sensor 10 late cycle 2 in miniature, one packet a minute: a midday overshoot, then an evening
// rebound of ~250 units (~17 g), every day. The held line must never rise; the unheld one does.
const day = [];
for (let m = 0; m < 1440; m += 1) {
  const h = m / 60;
  let v = 15000;
  if (h >= 11 && h < 15) v -= ((h - 11) / 4) * 500;    // the midday drop, overshooting
  else if (h >= 15) v -= 500 - Math.min(1, (h - 15) / 6) * 250;  // rebound of half of it
  day.push(v);
}
const days = [];
for (let d = 0; d < 4; d += 1) days.push(...day.map((v) => v - d * 250));
const heldDays = context.runFilter(timed(days), msv4Params).map((point) => point.v);
const freeDays = context.runFilter(timed(days, false), msv4Params).map((point) => point.v);
const maxRise = (series) => series.reduce((best, v, i) => Math.max(best, i ? v - Math.min(...series.slice(Math.max(0, i - 600), i)) : 0), 0);
assert.ok(heldDays.slice(5).every((v, i, a) => i === 0 || v <= a[i - 1]), "MSV4 held display must never rise between waterings.");
assert.ok(maxRise(freeDays) >= 200, "The unheld filter must still show the rebound (the test must exercise it).");
assert.ok(Math.abs(heldDays.at(-1) - freeDays.at(-1)) <= 260, "The held value must stay within one rebound of the tracker.");

// Stationary Alocasia-GP-like noise with the hold: bounded by the tracker's lower edge, no
// walk-down, and no false watering snap (the 2026-08-22 ratchet reached 879 here).
const heldStationary = context.runFilter(timed(stationaryRaw), defaults).map((point) => point.v);
assert.ok(Math.min(...heldStationary.slice(-300)) >= 890, "The hold must not walk stationary noise down to its lower envelope.");
assert.ok(heldStationary.slice(5).every((v, i, a) => i === 0 || v <= a[i - 1]), "Stationary noise must not trigger a false watering snap under the hold.");

// A real watering still snaps up through the hold.
const heldWatering = context.runFilter(timed([...Array(12).fill(1000), ...Array(8).fill(1300)]), { ...defaults, learnedSpan: 300 });
assert.equal(heldWatering[15].v, 1300, "A watering must still snap up on the fourth high packet with the hold on.");

// A slow lasting rise with no watering (a new insertion settling) is released after 36 h.
const settling = [];
for (let m = 0; m < 48 * 60; m += 1) settling.push(10000 + Math.round(Math.min(1, m / (6 * 60)) * 400));
const heldSettling = context.runFilter(timed(settling), msv4Params).map((point) => point.v);
assert.equal(heldSettling[30 * 60], heldSettling[10], "Before the release time the hold keeps the starting value.");
assert.ok(heldSettling.at(-1) >= 10390, "After 36 h above the hold, a lasting rise must be shown.");

console.log("PASS receiver/browser moisture filter is robust, centered, symmetric, and watering-responsive; MSV4 drying hold holds");
