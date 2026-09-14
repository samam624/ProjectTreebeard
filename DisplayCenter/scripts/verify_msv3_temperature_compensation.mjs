import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const displayCenterRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const firmware = await readFile(join(
  displayCenterRoot,
  "arduino",
  "Treebeard_ESP32_Display_Runtime",
  "Treebeard_ESP32_Display_Runtime.ino",
), "utf8");
const app = await readFile(join(
  displayCenterRoot,
  "arduino",
  "Treebeard_ESP32_Display_Runtime",
  "MobileAppPage.h",
), "utf8");
const browserScript = app.match(/<script>([\s\S]*?)<\/script>/)?.[1];
assert.ok(browserScript, "Could not extract the embedded History browser script.");
new vm.Script(browserScript);

assert.match(firmware, /TEMPERATURE_COMPENSATION_TEMP_TAU_SECONDS = 2\.0f \* 60\.0f \* 60\.0f/);
assert.match(firmware, /TEMPERATURE_COMPENSATION_MOISTURE_TAU_SECONDS = 48\.0f \* 60\.0f \* 60\.0f/);
assert.match(firmware, /TEMPERATURE_COMPENSATION_REQUIRED_FITS = 2/);
assert.match(firmware, /TEMPERATURE_COMPENSATION_MODEL_VERSION = 3/);
assert.match(firmware, /TEMPERATURE_COMPENSATION_PROBE_4_REFERENCE_C = 22\.34f/);

// --- v3 regression guards -------------------------------------------------------------------
// v2 seeded every probe with a POSITIVE gain and then rejected any fit that came back negative.
// Probe 8's real coefficient is negative, so all four of its 24-hour fits were rejected, its gain
// stayed pinned at the seed, and it applied a wrong-signed correction that made its daily swing
// 2.5x worse than no compensation at all. Each assertion below pins one half of that fix.

// 1. An unlearned probe must correct nothing rather than guess a sign it cannot know.
assert.match(
  firmware,
  /TEMPERATURE_COMPENSATION_DEFAULT_SEED_GAIN_PER_C = 0\.0f/,
  "The default seed gain must be zero; a fleet-average gain has no defined sign.",
);
assert.match(
  firmware,
  /TEMPERATURE_COMPENSATION_PROBE_4_SEED_GAIN_PER_C = 0\.0f/,
  "Per-probe seeds must also be zero until a probe has earned its own fit.",
);

// 2. The accepted gain range must be symmetric. A hard 0.0f lower bound is the original defect.
assert.match(
  firmware,
  /TEMPERATURE_COMPENSATION_MIN_GAIN_PER_C = -0\.04f/,
  "The gain bound must be symmetric so correctly fitted negative gains are applied, not discarded.",
);
assert.doesNotMatch(
  firmware,
  /candidateGain >= 0\.0f/,
  "Fit acceptance must not reject negative gains; the sign is a per-probe physical property.",
);
assert.doesNotMatch(
  firmware,
  /!isfinite\(gain\) \|\| gain < 0\.0f/,
  "Restore validation must not reject a persisted negative gain.",
);

// 3. The first accepted fit is adopted outright. Blending 3:1 against a zero seed would leave a
//    probe applying a quarter of its own measured gain for days.
assert.match(
  firmware,
  /state\.acceptedFits == 0[\s\S]{0,200}state\.gainPerC = candidateGain;/,
  "The first accepted fit must be taken outright rather than blended against the zero seed.",
);

// 4. Changing the model must invalidate persisted v2 state rather than reinterpret it.
assert.match(
  firmware,
  /TEMPERATURE_COMPENSATION_CACHE_PATH\[\] = "\/temp-comp-v3\.json"/,
  "A model change must move the cache path so v2 state is not restored under v3 semantics.",
);
assert.match(firmware, /TEMPERATURE_COMPENSATION_PERSIST_INTERVAL_MS = 15UL \* 60UL \* 1000UL/);
assert.match(
  firmware,
  /reading\.measuredMoisture = moisture;/,
  "The uncompensated receiver-derived measurement must remain independently published.",
);
assert.match(
  firmware,
  /useTemperatureCompensatedPresentation =[\s\S]*?effectiveMode == TEMPERATURE_COMPENSATION_ACTIVE/,
  "Only effective Active mode may feed compensation into the display filter.",
);
assert.match(firmware, /temperatureCompensatedLogRatioPpm/);
assert.match(firmware, /serviceTemperatureCompensationPersistence\(\);/);
assert.match(
  firmware,
  /row\["blockElapsedMs"\][\s\S]*?row\["sampleCount"\][\s\S]*?createNestedArray\("normal"\)[\s\S]*?createNestedArray\("rhs"\)/,
  "Partial regression duration and sufficient statistics must be checkpointed.",
);
assert.match(
  firmware,
  /state\.blockStartMs = nowMs - state\.restoredBlockElapsedMs/,
  "A reboot must resume the persisted amount of observed training time.",
);
assert.match(
  firmware,
  /connectedStep > TEMPERATURE_COMPENSATION_MAX_CONNECTED_STEP_FRACTION[\s\S]*?bareStep > TEMPERATURE_COMPENSATION_MAX_BARE_STEP_FRACTION/,
  "Large physical/reference steps must start a new model epoch.",
);
assert.match(firmware, /if \(probeRestarted\) resetReason = "probe-restart"/);
assert.match(
  firmware,
  /state\.gainPerC \* slowExcess \* \(state\.effectiveTempC - state\.referenceTempC\)/,
  "Correction must use the persisted calibration reference, not a hard-coded temperature.",
);
assert.match(app, /\["learning", "Learning \(shadow only\)"\]/);
assert.match(app, /historyTemperatureCompensatedSection/);
assert.match(app, /TEMPERATURE_COMPENSATION_TREND_TIME_CONSTANT_MS = 12 \* 60 \* 60 \* 1000/);
assert.match(app, /raw: msv3ReferenceExcessPercent\(r\)/);
assert.match(app, /trend \+= alpha \* \(point\.v - trend\)/);

const helperSource = app.match(/function msv3TemperatureCompensatedExcessPercent\(row\) \{[\s\S]*?\n    \}/)?.[0];
assert.ok(helperSource, "Could not extract the compensated-history conversion helper.");
const context = {};
vm.createContext(context);
vm.runInContext(`${helperSource}\nthis.convert = msv3TemperatureCompensatedExcessPercent;`, context);
assert.equal(context.convert(null), null);
assert.equal(context.convert({ temperature_compensated_log_ratio_ppm: null }), null);
const expected = (Math.exp(0.25) - 1) * 100;
assert.ok(Math.abs(context.convert({ temperature_compensated_log_ratio_ppm: 250_000 }) - expected) < 1e-12);

const trendSource = app.match(/function addExponentialMovingAverageTrend\(points, timeConstantMs\) \{[\s\S]*?\n    \}/)?.[0];
assert.ok(trendSource, "Could not extract the continuous trend helper.");
const trendContext = {};
vm.createContext(trendContext);
vm.runInContext(`${trendSource}\nthis.trend = addExponentialMovingAverageTrend;`, trendContext);
const hour = 60 * 60 * 1000;
const trend = trendContext.trend([
  { t: 0, v: 1 },
  { t: hour, v: 100 },
  { t: 2 * hour, v: 3 },
  { t: 14 * hour, v: 5 },
], 12 * hour);
assert.equal(trend[0].trend, 1, "The EWMA must begin at the first real sample without a synthetic ramp.");
assert.ok(
  trend[1].trend > 1 && trend[1].trend < 100,
  "A new sample must move the trend continuously instead of replacing it like a median rank change.",
);
const beforeGap = trend[2].trend;
const expectedAfterGap = beforeGap + (1 - Math.exp(-1)) * (5 - beforeGap);
assert.ok(
  Math.abs(trend[3].trend - expectedAfterGap) < 1e-12,
  "The EWMA must use elapsed time, including a full-time-constant history gap.",
);
assert.match(app, /12 h EWMA moisture trend/);

console.log("PASS MSV3 temperature compensation v3 is gated, sign-correct, restart-resumable, epoch-safe, and continuously trended");
