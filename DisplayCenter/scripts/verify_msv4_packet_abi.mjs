// The MSV4 production advertisement is an ABI shared by five readers in four subsystems:
//
//   1. MSV4/arduino/MSV4_Moisture_BLE_Sleep/MSV4_Moisture_BLE_Sleep.ino   startAdv()   (writer)
//   2. DisplayCenter/.../Treebeard_ESP32_Display_Runtime.ino   onResult()              (reader)
//   3. MSV4/scripts/msv4_adv_scan.py                           decode()                (reader)
//   4. cloud/supabase/functions/receiver-snapshot/msv3-diagnostics.ts   (stores it)
//   5. DisplayCenter/.../MobileAppPage.h                       History page            (reader)
//
// It reuses the MSV3 production framing and the "MSV2" magic, and says "these bytes mean something
// else" with flag bit7 alone. That makes one-sided drift unusually dangerous here: a receiver that
// misses the flag stores ohms as FDC counts under a real plant's history. This is a SOURCE-TEXT
// check, like its sibling verifiers. msv4_adv_scan.py --self-test and
// verify_msv4_history_decode.mjs cover the arithmetic; this proves the files still agree on
// offsets, flags, the saturation value and the scale version.
//
// Three packet forms exist: 23 bytes (application 262148), 26 bytes layout A (262150, adds die
// temperature) and 26 bytes layout B (262152, flags2 bit1: Cd replaces Cp, the residual moves).
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

// ---- Framing: production magic, and lengths every reader can handle ----
assert.match(probe, /const uint8_t BLE_MAGIC\[4\] = \{'M', 'S', 'V', '2'\};/,
  "MSV4 production must use the production magic; the bench magic never reaches plant history.");
const probeLen = numberFrom(probe, /constexpr size_t MFG_LEN = (\d+);/, "probe MFG_LEN");
assert.equal(probeLen, 26);
assert.match(receiver, /const size_t MSV3_DIAGNOSTIC_PACKET_LEN = 23;/);
assert.equal(
  numberFrom(receiver, /const size_t MSV4_EXTENDED_PACKET_LEN = (\d+);/, "receiver extended length"), probeLen);
assert.equal(numberFrom(scanner, /^PAYLOAD_LEN = (\d+)$/m, "scanner PAYLOAD_LEN"), 23 - 2,
  "Bleak strips the two company-ID bytes, so the scanner's payload is the probe's minus two.");
assert.equal(numberFrom(scanner, /^EXTENDED_LEN = (\d+)$/m, "scanner EXTENDED_LEN"), probeLen - 2);
// 31-byte legacy PDU: Flags(3) + manufacturer header(2) + 26. There is no room for anything else,
// and the TX Power structure that MSV3 carries is what made way for the three extra bytes.
assert.doesNotMatch(probe, /Advertising\.addTxPower\(\)/,
  "A 26-byte MSV4 packet only fits if the TX Power AD structure stays out of the advertisement.");

// ---- The flag that changes the meaning of every sensor field ----
assert.match(probe, /#define ADV_FLAG_MSV4\s+\(1U << 7\)/);
assert.match(probe, /mfg\[16\] = ADV_FLAG_MSV4 \|/, "Every MSV4 packet must carry bit7.");
assert.match(receiver, /const uint8_t MSV4_ADV_FLAG = 0x80;/);
assert.match(receiver, /msv4 = \(flags & MSV4_ADV_FLAG\) != 0;/);
assert.match(scanner, /^FLAG_MSV4 = 1 << 7$/m);
// Bits an MSV3-aware receiver acts on must stay clear: bit3 would validate the diagnostic bytes
// as a bare-tank count and bit6 would select the capless FDC scale.
assert.doesNotMatch(probe, /#define ADV_FLAG_\w+\s+\(1U << [36]\)/,
  "MSV4 must never set the MSV3 bare-tank-valid or capless bits.");

// ---- Fields common to every layout (probe mfg[] index; the scanner's are these minus two) ----
assert.match(probe, /mfg\[10\] = \(uint8_t\)\(rsoil & 0xFF\);/);
assert.match(probe, /mfg\[12\] = \(uint8_t\)\(\(rsoil >> 16\) & 0xFF\);/);
assert.match(probe, /mfg\[14\] = \(uint8_t\)\(batteryMv & 0xFF\);/);
assert.match(probe, /mfg\[17\] = \(uint8_t\)\(\(uint16_t\)tempQ4 & 0xFF\);/);
assert.match(probe, /mfg\[19\] = \(uint8_t\)\(cm & 0xFF\);/);
assert.match(scanner, /struct\.unpack_from\("<IH", payload, 8\)/, "Rsoil word + battery at mfg[10]");
assert.match(scanner, /struct\.unpack_from\("<hH", payload, 15\)/, "tip temp + Cm at mfg[17]");

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

// ---- The resistance is 24 bits; byte 13 is not part of it ----
// Layout B puts the fit residual there. Every reader must split the 32-bit field before judging
// it as a resistance, in every layout.
assert.match(probe, /static_assert\(RSOIL_DECIOHM_MAX < \(1UL << 24\)/);
assert.match(receiver, /const uint32_t MSV4_RSOIL_MASK = 0x00FFFFFFUL;/);
assert.match(receiver,
  /const uint8_t msv4TopByte = \(uint8_t\)\(rawFdcCounts >> 24\);\s*rawFdcCounts &= MSV4_RSOIL_MASK;/,
  "The receiver must strip byte 13 out of the field before the resistance range check.");
assert.match(scanner, /rsoil_deci = rsoil_word & 0xFFFFFF/);

// ---- Layout B (flags2 bit1): residual in byte 13, Cd in bytes 21-22, no Cp ----
assert.match(probe, /#define ADV_FLAG2_LAYOUT_B\s+\(1U << 1\)/);
assert.match(probe, /mfg\[25\] = ADV_FLAG2_LAYOUT_B \| \(dieTempOk \? ADV_FLAG2_DIE_TEMP_OK : 0\)/,
  "This sketch only ever writes layout B, so it must always say so.");
assert.match(probe, /mfg\[13\] = reading\.valid \? reading\.rmsE4 : 0;/);
assert.match(probe, /mfg\[21\] = \(uint8_t\)\(cd & 0xFF\);/);
assert.match(probe, /mfg\[22\] = \(uint8_t\)\(\(cd >> 8\) & 0xFF\);/);
assert.match(receiver, /const uint8_t MSV4_ADV_FLAG2_LAYOUT_B = 0x02;/);
assert.match(receiver,
  /\(buffer\[25\] & MSV4_ADV_FLAG2_LAYOUT_B\) != 0\)\s*\{[\s\S]{0,160}msv4FitRmsE4 = msv4TopByte;\s*rawBareTankFdcCounts = bareTankField;/);
assert.match(scanner, /^FLAG2_LAYOUT_B = 1 << 1$/m);
assert.match(scanner, /rms_e4, cp_deci = rsoil_word >> 24, None\s*cd_centi = struct\.unpack_from\("<H", payload, 19\)\[0\]/);

// ---- Layout A (262148/262150) is still in the field and in stored history ----
// Cm, then Cp, then the residual. The receiver converts it to the stored form on arrival.
assert.match(receiver,
  /msv4CpDeciPf = \(uint8_t\)\(\(bareTankField >> 16\) & 0xFFUL\);\s*msv4FitRmsE4 = \(uint8_t\)\(bareTankField >> 24\);\s*rawBareTankFdcCounts = bareTankField & 0xFFFFUL;/);
assert.match(scanner, /cp_deci, rms_e4, cd_centi = payload\[19\], payload\[20\], 0/);

// ---- Die temperature (bytes 23-25): optional tail, its own validity bit ----
assert.match(probe, /mfg\[23\] = \(uint8_t\)\(\(uint16_t\)dieQ4 & 0xFF\);/);
assert.match(probe, /#define ADV_FLAG2_DIE_TEMP_OK\s+\(1U << 0\)/);
assert.match(receiver, /const uint8_t MSV4_ADV_FLAG2_DIE_TEMP = 0x01;/);
assert.match(receiver,
  /len >= MSV4_EXTENDED_PACKET_LEN && \(buffer\[25\] & MSV4_ADV_FLAG2_DIE_TEMP\) != 0[\s\S]{0,260}buffer\[23\] \| \(buffer\[24\] << 8\)/);
assert.match(scanner, /flags2 = payload\[23\] if len\(payload\) >= EXTENDED_LEN else 0/);
assert.match(scanner, /if flags2 & FLAG2_DIE_TEMP_OK:\s*die_c = struct\.unpack_from\("<h", payload, 21\)/);

// ---- Short bursts (flags2 bit2): the packet says when nothing can connect ----
// From application 262154 most wakes advertise non-connectably. An updater that tried to connect
// on one would burn its retries, so the probe marks them and both host tools wait for a packet
// without the mark. The receiver ignores the bit: the readings are the same either way.
const updater = await readFile(join(repoRoot, "MSV4", "scripts", "msv4_ble_ota.py"), "utf8");
assert.match(probe, /#define ADV_FLAG2_SHORT_BURST\s+\(1U << 2\)/);
assert.match(probe, /\(longWindow \? 0 : ADV_FLAG2_SHORT_BURST\)/);
assert.match(probe, /BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED/);
assert.match(scanner, /^FLAG2_SHORT_BURST = 1 << 2$/m);
assert.match(updater, /^SHORT_BURST_FLAG2 = 0x04$/m);
assert.match(updater, /payload\[23\] & SHORT_BURST_FLAG2/);
// A measurement wake must always open the connectable window: it is the only scheduled way in.
assert.match(probe,
  /const bool longWindow = g_wakesUntilMoisture == 0 \|\| g_wakesSinceBoot < LONG_WINDOW_BOOT_WAKES;/);

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

// ---- The receiver's history record borrows two MSV3 slots; neither may leak ----
// rawFdcCounts keeps the residual in its top byte, so what is uploaded as fdcCounts is masked.
assert.equal(
  (receiver.match(/historyFdcCountsField\(pending\.rawFdcCounts, pending\.moistureScale\)/g) || []).length,
  2, "Both history serializers must mask the residual out of raw.fdcCounts.");
// rawMoistureKHz keeps the die temperature, which must never be decoded as MSV3's log ratio.
assert.equal(
  (receiver.match(/pending\.wideRaw && pending\.moistureScale != MSV4_MOISTURE_SCALE_VERSION &&\s*decodeTemperatureCompensatedLogRatioPpm/g) || []).length,
  2, "Both history serializers must skip the log-ratio decode for MSV4 rows.");

// ---- The temperature correction is applied off the probe, identically in both readers ----
assert.doesNotMatch(probe, /TEMPCO|RsoilTo25C/, "The probe sends Rsoil uncorrected.");
assert.match(receiver, /const float MSV4_EC_TEMPCO_PER_C = 0\.02f;/);
assert.match(scanner, /^EC_TEMPCO_PER_C = 0\.02$/m);

// ---- The History page and the cloud function read the same quantities back out ----
//
// History rows carry no flag bits, only moisture_scale, and four columns whose meaning depends on
// it. The page re-derives battery percent and the 25 C correction from raw values, so its
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
// The stored word: Cm in bits 0-15, Cp 16-23, residual 24-31, Cd (0.01 pF) 32-47. The receiver
// sends named fields; the cloud function packs them and the page unpacks them.
for (const key of ["cmDeciPf", "fitRmsE4", "cdCentiPf", "cpDeciPf", "dieTempQuarterC"]) {
  assert.ok(receiver.includes(`\\"${key}\\":`), `receiver must emit raw.msv4.${key}`);
  assert.ok(cloud.includes(`msv4?.${key}`), `cloud function must read raw.msv4.${key}`);
}
assert.match(cloud, /return cm \+ cp \* 0x10000 \+ rms \* 0x1000000 \+ cd \* 0x100000000;/);
assert.match(page, /cmPf: \(word % 65536\) \/ 10,/);
assert.match(page, /const cpDeciPf = Math\.floor\(word \/ 65536\) % 256;/);
assert.match(page, /residualPercent: \(Math\.floor\(word \/ 16777216\) % 256\) \/ 100,/);
assert.match(page, /const cdCentiPf = Math\.floor\(word \/ 4294967296\) % 65536;/);
// Die temperature: probe field -> raw.msv4.dieTempQuarterC -> raw_moisture_khz -> the page.
assert.match(cloud, /export function parseMsv4DieTempQuarterC/);
assert.match(page, /function msv4DieTempC\(row\) \{[\s\S]{0,200}row\.raw_moisture_khz/);
// The MSV3 cards must not chart an MSV4 row's resistance or packed word as FDC counts.
assert.match(page, /r\.raw_fdc_counts != null && !isMsv4Row\(r\)/);
assert.match(page, /r\.bare_tank_fdc_counts != null && !isMsv4Row\(r\)/);
assert.match(page, /function msv3ReferenceExcessPercent\(row\) \{\s*if \(!row\) return null;[\s\S]{0,400}if \(Number\(row\.moisture_scale\) === 4\) return null;/);

console.log("MSV4 packet ABI: probe, receiver, scanner, History page and cloud function agree.");
