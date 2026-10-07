// The MSV4 production advertisement is an ABI shared by three decoders in three subsystems:
//
//   1. MSV4/arduino/MSV4_Moisture_BLE_Sleep/MSV4_Moisture_BLE_Sleep.ino   startAdv()   (writer)
//   2. DisplayCenter/.../Treebeard_ESP32_Display_Runtime.ino   onResult()              (reader)
//   3. MSV4/scripts/msv4_adv_scan.py                           decode()                (reader)
//
// It reuses the 23-byte MSV3 production layout and the "MSV2" magic, and says "these bytes mean
// something else" with flag bit7 alone. That makes one-sided drift unusually dangerous here: a
// receiver that misses the flag stores ohms as FDC counts under a real plant's history. This is a
// SOURCE-TEXT check, like its sibling verifiers. msv4_adv_scan.py --self-test covers the decode
// arithmetic; this proves the three files still agree on offsets, the flag, the saturation value
// and the scale version.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const displayCenterRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const repoRoot = resolve(displayCenterRoot, "..");

const probe = await readFile(join(
  repoRoot, "MSV4", "arduino", "MSV4_Moisture_BLE_Sleep", "MSV4_Moisture_BLE_Sleep.ino",
), "utf8");
const receiver = await readFile(join(
  displayCenterRoot, "arduino", "Treebeard_ESP32_Display_Runtime",
  "Treebeard_ESP32_Display_Runtime.ino",
), "utf8");
const scanner = await readFile(join(repoRoot, "MSV4", "scripts", "msv4_adv_scan.py"), "utf8");
const page = await readFile(join(
  displayCenterRoot, "arduino", "Treebeard_ESP32_Display_Runtime", "MobileAppPage.h",
), "utf8");
const cloud = await readFile(join(
  repoRoot, "cloud", "supabase", "functions", "receiver-snapshot", "msv3-diagnostics.ts",
), "utf8");

function numberFrom(source, pattern, label) {
  const match = source.match(pattern);
  assert.ok(match, `${label} not found`);
  return Number(match[1].replace(/_/g, ""));
}

// ---- Framing: production magic, 23 bytes, so the receiver's existing length logic applies ----
assert.match(probe, /const uint8_t BLE_MAGIC\[4\] = \{'M', 'S', 'V', '2'\};/,
  "MSV4 production must use the production magic; the bench magic never reaches plant history.");
// Two lengths are in the field: 23 bytes (application 262148) and 26 (262150 onward, which adds
// die temperature). Every reader must accept the short one and treat the tail as optional.
const probeLen = numberFrom(probe, /constexpr size_t MFG_LEN = (\d+);/, "probe MFG_LEN");
assert.equal(probeLen, 26);
assert.match(receiver, /const size_t MSV3_DIAGNOSTIC_PACKET_LEN = 23;/);
assert.equal(
  numberFrom(receiver, /const size_t MSV4_EXTENDED_PACKET_LEN = (\d+);/, "receiver extended length"), probeLen);
assert.equal(numberFrom(scanner, /^PAYLOAD_LEN = (\d+)$/m, "scanner PAYLOAD_LEN"), 23 - 2,
  "Bleak strips the two company-ID bytes, so the scanner's payload is the probe's minus two.");
assert.equal(numberFrom(scanner, /^EXTENDED_LEN = (\d+)$/m, "scanner EXTENDED_LEN"), probeLen - 2);
// 31-byte legacy PDU: Flags(3) + manufacturer header(2) + 26. There is no room for anything else,
// and the TX Power structure that MSV3 carries is what made way for the three new bytes.
assert.doesNotMatch(probe, /Advertising\.addTxPower\(\)/,
  "A 26-byte MSV4 packet only fits if the TX Power AD structure stays out of the advertisement.");

// ---- Die temperature (bytes 23-25): optional tail, its own validity bit ----
assert.match(probe, /mfg\[23\] = \(uint8_t\)\(\(uint16_t\)dieQ4 & 0xFF\);/);
assert.match(probe, /mfg\[25\] = dieTempOk \? ADV_FLAG2_DIE_TEMP_OK : 0;/);
assert.match(probe, /#define ADV_FLAG2_DIE_TEMP_OK\s+\(1U << 0\)/);
assert.match(receiver, /const uint8_t MSV4_ADV_FLAG2_DIE_TEMP = 0x01;/);
assert.match(receiver,
  /len >= MSV4_EXTENDED_PACKET_LEN && \(buffer\[25\] & MSV4_ADV_FLAG2_DIE_TEMP\) != 0[\s\S]{0,260}buffer\[23\] \| \(buffer\[24\] << 8\)/);
assert.match(scanner, /len\(payload\) >= EXTENDED_LEN and payload\[23\] & FLAG2_DIE_TEMP_OK/);
assert.match(scanner, /struct\.unpack_from\("<h", payload, 21\)/);
// In history the die temperature borrows the slot MSV3 uses for a log ratio, so an MSV4 row must
// never be decoded as one.
assert.equal(
  (receiver.match(/pending\.wideRaw && pending\.moistureScale != MSV4_MOISTURE_SCALE_VERSION &&\s*decodeTemperatureCompensatedLogRatioPpm/g) || []).length,
  2, "Both history serializers must skip the log-ratio decode for MSV4 rows.");

// ---- The flag that changes the meaning of every sensor field ----
assert.match(probe, /#define ADV_FLAG_MSV4\s+\(1U << 7\)/);
assert.match(probe, /mfg\[16\] = ADV_FLAG_MSV4 \|/, "Every MSV4 packet must carry bit7.");
assert.match(receiver, /const uint8_t MSV4_ADV_FLAG = 0x80;/);
assert.match(receiver, /msv4 = \(flags & MSV4_ADV_FLAG\) != 0;/);
assert.match(scanner, /^FLAG_MSV4 = 1 << 7$/m);
// Bits an MSV3-aware receiver acts on must stay clear: bit3 would validate the diagnostic bytes
// as a bare-tank count and bit6 would select the capless FDC scale.
assert.doesNotMatch(probe, /\(1U << 3\)|\(1U << 6\)/,
  "MSV4 must never set the MSV3 bare-tank-valid or capless bits.");

// ---- Field offsets (probe mfg[] index; the scanner's are these minus two) ----
assert.match(probe, /mfg\[10\] = \(uint8_t\)\(rsoil & 0xFF\);/);
assert.match(probe, /mfg\[13\] = \(uint8_t\)\(\(rsoil >> 24\) & 0xFF\);/);
assert.match(probe, /mfg\[14\] = \(uint8_t\)\(batteryMv & 0xFF\);/);
assert.match(probe, /mfg\[17\] = \(uint8_t\)\(\(uint16_t\)tempQ4 & 0xFF\);/);
assert.match(probe, /mfg\[19\] = \(uint8_t\)\(cm & 0xFF\);/);
assert.match(probe, /mfg\[21\] = cpDeci >= 255\.0 \? 255 : \(uint8_t\)cpDeci;/);
assert.match(probe, /mfg\[22\] = reading\.valid \? reading\.rmsE4 : 0;/);
assert.match(scanner, /struct\.unpack_from\("<IH", payload, 8\)/, "Rsoil + battery at mfg[10]");
assert.match(scanner, /struct\.unpack_from\("<hH", payload, 15\)/, "tip temp + Cm at mfg[17]");
assert.match(scanner, /cp_deci, rms_e4 = payload\[19\], payload\[20\]/);
// The receiver reads bytes 19-22 as one little-endian word, so Cm is the low half, then Cp, then
// the residual.
assert.match(receiver, /rawBareTankFdcCounts = bareTankField;/);
assert.match(receiver, /msv4CmDeciPf\(uint32_t packed\) \{ return \(uint16_t\)\(packed & 0xFFFFUL\); \}/);
assert.match(receiver, /msv4CpDeciPf\(uint32_t packed\) \{ return \(uint8_t\)\(\(packed >> 16\) & 0xFFUL\); \}/);
assert.match(receiver, /msv4FitRmsE4\(uint32_t packed\) \{ return \(uint8_t\)\(\(packed >> 24\) & 0xFFUL\); \}/);

// ---- Saturation value, and the property that keeps an older receiver safe ----
const probeMax = numberFrom(probe, /constexpr uint32_t RSOIL_DECIOHM_MAX = (\d+)UL;/, "probe max");
const receiverMax = numberFrom(receiver, /const uint32_t MSV4_RSOIL_DECIOHM_MAX = (\d+)UL;/, "receiver max");
const scannerMax = numberFrom(scanner, /^RSOIL_DECIOHM_MAX = ([\d_]+)$/m, "scanner max");
assert.equal(receiverMax, probeMax);
assert.equal(scannerMax, probeMax);
const msv3Floor = numberFrom(receiver, /const uint32_t MSV3_PLAUSIBLE_COUNT_MIN = (\d+)UL;/, "MSV3 floor");
assert.ok(probeMax < msv3Floor,
  "Rsoil must saturate below the MSV3 FDC plausibility floor, so a receiver that predates bit7 " +
  "rejects an MSV4 packet instead of storing ohms as counts.");

// ---- An MSV4 packet must not be judged or stored as an FDC reading ----
assert.match(receiver, /if \(msv4\)\s*\{\s*caplessTank = false;\s*bareTankValid = false;/);
assert.match(receiver,
  /rawFdcCounts >= MSV4_RSOIL_DECIOHM_MIN &&\s*rawFdcCounts <= MSV4_RSOIL_DECIOHM_MAX;/);
assert.match(receiver, /moisture = calibrateMsv4Moisture\(rawFdcCounts, boardTempValid, rawBoardTempQuarterC\);/);
assert.match(receiver, /const uint8_t MSV4_MOISTURE_SCALE_VERSION = 4;/);
assert.match(receiver, /if \(msv4\) return MSV4_MOISTURE_SCALE_VERSION;/,
  "MSV4 history must carry its own scale version: it is the only thing that tells the cloud " +
  "raw_fdc_counts holds a resistance.");
assert.match(receiver, /reading\.msv4 != msv4\)\)/,
  "A sensor ID changing generation must reset the presentation filter, not blend two scales.");

// ---- The temperature correction is applied off the probe, identically in both readers ----
assert.doesNotMatch(probe, /TEMPCO|RsoilTo25C/, "The probe sends Rsoil uncorrected.");
assert.match(receiver, /const float MSV4_EC_TEMPCO_PER_C = 0\.02f;/);
assert.match(scanner, /^EC_TEMPCO_PER_C = 0\.02$/m);

// ---- The History page and the cloud function read the same quantities back out ----
//
// History rows carry no flag bits, only moisture_scale, and three columns whose meaning depends
// on it. The page re-derives battery percent and the 25 C correction from raw values, so its
// constants are hand ports of the firmware's and can drift exactly like the battery curve did.
const pageNumber = (name) =>
  numberFrom(page, new RegExp(String.raw`const ${name} = ([\d.]+);`), `page ${name}`);
const firmwareNumber = (name) =>
  numberFrom(receiver, new RegExp(String.raw`const \w+ ${name} = ([\d.]+)(?:UL|f)?;`), `firmware ${name}`);
for (const name of [
  "MSV4_RSOIL_DECIOHM_MAX", "MSV4_EC_TEMPCO_PER_C", "MSV4_EC_REFERENCE_C",
  "MSV4_BATTERY_EMPTY_MV", "MSV4_BATTERY_FULL_MV",
]) {
  assert.equal(pageNumber(name), firmwareNumber(name), `History page ${name} must match the firmware`);
}
assert.equal(pageNumber("MSV4_MOISTURE_SCALE"), 4);
assert.equal(numberFrom(cloud, /export const MSV4_MOISTURE_SCALE = (\d+);/, "cloud scale"), 4);
// Cm low 16 bits, Cp next 8, residual top 8 - the same word in all three places.
assert.match(cloud, /return cm \+ cp \* 0x10000 \+ rms \* 0x1000000;/);
assert.match(page, /cmPf: \(word % 65536\) \/ 10,/);
assert.match(page, /cpPf: \(Math\.floor\(word \/ 65536\) % 256\) \/ 10,/);
assert.match(page, /residualPercent: \(Math\.floor\(word \/ 16777216\) % 256\) \/ 100,/);
// Die temperature: probe field -> raw.msv4.dieTempQuarterC -> raw_moisture_khz -> the page.
assert.match(receiver, /output \+= F\(",\\"dieTempQuarterC\\":"\);/);
assert.match(cloud, /export function parseMsv4DieTempQuarterC/);
assert.match(page, /function msv4DieTempC\(row\) \{[\s\S]{0,200}row\.raw_moisture_khz/);
// The MSV3 cards must not chart an MSV4 row's resistance or packed word as FDC counts.
assert.match(page, /r\.raw_fdc_counts != null && !isMsv4Row\(r\)/);
assert.match(page, /r\.bare_tank_fdc_counts != null && !isMsv4Row\(r\)/);
assert.match(page, /function msv3ReferenceExcessPercent\(row\) \{\s*if \(!row\) return null;[\s\S]{0,400}if \(Number\(row\.moisture_scale\) === 4\) return null;/);

console.log("MSV4 packet ABI: probe, receiver, scanner, History page and cloud function agree.");
