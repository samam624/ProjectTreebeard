import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const displayCenterRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const repoRoot = resolve(displayCenterRoot, "..");
const receiver = await readFile(join(
  displayCenterRoot,
  "arduino",
  "Treebeard_ESP32_Display_Runtime",
  "Treebeard_ESP32_Display_Runtime.ino",
), "utf8");
const probe = await readFile(join(
  repoRoot,
  "MSV3",
  "arduino",
  "MSV3_Moisture_BLE_Sleep",
  "MSV3_Moisture_BLE_Sleep.ino",
), "utf8");

// Production may select the connected electrode phase or park the mux, and nothing else. The
// bring-up console deliberately remains able to select individual codes for engineering work.
const muxSelections = [...probe.matchAll(/\bsetMux\(([^)]+)\);/g)].map((match) => match[1].trim());
assert.deepEqual(
  [...new Set(muxSelections)].sort(),
  ["MUX_SLEEP_CODE", "phaseUsed"],
  "Production probe firmware must never select the disconnected bare-tank mux codes.",
);
assert.doesNotMatch(probe, /bareTankMuxCode|ADV_FLAG_BARE_TANK_OK/);
assert.match(probe, /constexpr uint32_t bareTankField = 0;/);
assert.match(probe, /#define ADV_FLAG_CAPLESS_TANK\s+\(1U << 6\)/);
assert.match(probe, /ADV_FLAG_CAPLESS_TANK\s*\|/);
assert.match(probe, /#define MSV3_FDC_IDRIVE_CODE 16/);
assert.match(probe, /const uint32_t MOISTURE_SWING_THRESHOLD = 1200000UL;/);

// The receiver must distinguish capless hardware before applying plausibility, scaling, and
// compensation rules. Legacy scale 2 remains available while the fleet is converted.
assert.match(receiver, /MSV3_CAPLESS_MOISTURE_SCALE_VERSION = 3/);
assert.match(receiver, /caplessTank = \(flags & 0x40\) != 0;/);
assert.match(receiver, /if \(caplessTank\)[\s\S]{0,160}bareTankValid = false;[\s\S]{0,100}rawBareTankFdcCounts = 0;/);
assert.match(receiver, /MSV3_CAPLESS_PLAUSIBLE_COUNT_MAX = 80000000UL/);
assert.match(receiver, /moisture = caplessTank[\s\S]{0,120}calibrateMsv3CaplessMoisture/);
assert.match(receiver, /if \(caplessTank\)[\s\S]{0,350}configuredMode = TEMPERATURE_COMPENSATION_OFF;[\s\S]{0,160}effectiveMode = TEMPERATURE_COMPENSATION_OFF;/);

const referenceMatch = receiver.match(/MSV3_CAPLESS_FDC_MOISTURE_REFERENCE_COUNTS = (\d+)UL/);
const divisorMatch = receiver.match(/MSV3_CAPLESS_FDC_COUNTS_PER_MOISTURE_UNIT = (\d+)UL/);
assert.ok(referenceMatch && divisorMatch, "Capless receiver calibration constants must exist.");
const reference = Number(referenceMatch[1]);
const divisor = Number(divisorMatch[1]);
const calibrate = (raw) => Math.min(0xffff, Math.max(0, Math.floor((reference - raw) / divisor)));
assert.equal(calibrate(39_400_000), 40_600, "Probe 8 dry air must remain inside scale 3.");
assert.equal(calibrate(27_000_000), 53_000, "Probe 8's two-hand result must remain inside scale 3.");
assert.equal(calibrate(80_000_000), 0, "Scale 3 must clamp at its dry reference.");

console.log("PASS MSV3 capless production forbids bare mux reads and receiver scale 3 preserves the measured range");
