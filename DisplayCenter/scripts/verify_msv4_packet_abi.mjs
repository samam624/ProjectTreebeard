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

// ---- DC/DC regulator state (flags2 bit3): a register readback, not a build constant ----
// From application 262155. The bit is only evidence if the probe reads DCDCEN when it builds the
// packet, so that is what is checked. The receiver ignores the bit and stores nothing for it.
assert.match(probe, /#define ADV_FLAG2_DCDC_ON\s+\(1U << 3\)/);
assert.match(probe, /\(dcdcEnabled\(\) \? ADV_FLAG2_DCDC_ON : 0\)/);
assert.match(probe, /return \(NRF_POWER->DCDCEN & POWER_DCDCEN_DCDCEN_Msk\) != 0;/);
assert.match(scanner, /^FLAG2_DCDC_ON = 1 << 3$/m);

// ---- Warm-start fit (flags2 bit4): which estimator path produced the moisture fields ----
// From application 262157. It travels with the reading, so it must come from the reading, and a
// reading that is not valid must not claim it. The receiver ignores the bit.
assert.match(probe, /#define ADV_FLAG2_FIT_WARM\s+\(1U << 4\)/);
assert.match(probe, /\(reading\.valid && reading\.warm \? ADV_FLAG2_FIT_WARM : 0\)/);
assert.match(scanner, /^FLAG2_FIT_WARM = 1 << 4$/m);
assert.match(scanner, /"fit_warm": bool\(valid and flags2 & FLAG2_FIT_WARM\)/);

// ---- Discharge burn (flags2 bit5): a test image saying it is drawing the cell down ----
// Only a build made with -BurnFloorMv contains the burn. A production build must compile it out
// completely, so the bit has to come from a function that is empty without the build parameter.
assert.match(probe, /#define ADV_FLAG2_BURN\s+\(1U << 5\)/);
assert.match(probe, /\(burnActive\(\) \? ADV_FLAG2_BURN : 0\)/);
assert.match(probe, /#else\s+static void burnBegin\(\) \{\}\s+static void burnJudge\(bool, float\) \{\}\s+static bool burnActive\(\) \{ return false; \}/);
assert.match(probe, /static_assert\(MSV4_BURN_FLOOR_MV >= 3600 &&/,
  "The burn floor must stay above the level where the harvester cuts the 3V3 rail.");
assert.match(scanner, /^FLAG2_BURN = 1 << 5$/m);

// ---- Cp not calibrated yet (flags2 bit6): Cd is the whole node capacitance ----
// From application 262160. Cp is measured only straight after a reading that found nothing
// conducting, never at boot, so the bit must follow the calibration state and the calibration
// must sit behind the open test. The receiver ignores the bit.
assert.match(probe, /#define ADV_FLAG2_CP_DEFAULT\s+\(1U << 6\)/);
assert.match(probe, /\(g_cpCalibrated \? 0 : ADV_FLAG2_CP_DEFAULT\)/);
assert.match(probe,
  /const bool saneOpenReading = rsoil >= FIT_OPEN_OHM && bestRms < SPEC_CAL_MAX_RMS;\s*if \(!g_cpCalibrated && saneOpenReading\) cd = calibrateCpAfterOpenReading\(cd\);/);
assert.equal(probe.match(/measureCp\(/g).length, 2,
  "measureCp() must have one caller: a calibration taken with something on the blade is how " +
  "board 1 came to report Cd 7 pF low on 2026-10-08.");
assert.match(probe, /constexpr double SPEC_CP_DEFAULT = 0\.0;/,
  "Until it is calibrated Cp is zero: a Cp above the node capacitance pins Cd and bends Rsoil.");
assert.match(scanner, /^FLAG2_CP_DEFAULT = 1 << 6$/m);

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

// ---- Mask check (application 262162): a DIS string, not a packet field ----
// The probe holds DRV high, reads SENSE against it with each plate hot and with both floating, and
// publishes "A+0.3 B-0.1 mV" in the GATT String characteristic. The scanner decodes exactly that
// text, so the two formats are one ABI. It runs after the sweep, on measurement wakes only.
assert.match(probe, /constexpr uint16_t UUID16_CHR_STRING = 0x2A3D;/);
assert.match(scanner, /^MASK_CHECK_UUID = "00002a3d-0000-1000-8000-00805f9b34fb"$/m);
assert.match(probe, /snprintf\(out, size, "%c%c%ld\.%ld", plate, sign, deci \/ 10, deci % 10\);/);
// From 262165 the text ends in the count of this boot's checks, so a reader can tell a new result
// from the one it read before; 262163's text has no count and must still decode.
assert.match(probe, /snprintf\(g_maskText, sizeof\(g_maskText\), "%s %s mV n%u", a, b, \(unsigned\)g_maskChecks\);/);
assert.ok(scanner.includes(String.raw`re.fullmatch(r"A([+-]\d+\.\d) B([+-]\d+\.\d) mV(?: n(\d+))?", text)`),
  "The scanner must decode the probe's mask text, with or without the count, and nothing looser.");
// Once a day and on a boot's first measurement wake, still after the sweep of the same wake.
assert.match(probe, /static uint16_t g_wakesSinceMaskCheck = MASK_CHECK_EVERY_WAKES;/);
assert.match(probe, /if \(maskCheckDue\(\)\) checkMask\(\);/);
assert.match(probe, /checkMask\(\);\s*\}\s*else\s*\{\s*g_wakesUntilMoisture--;/,
  "The DC step belongs after the sweep of the same wake, never before it.");
assert.match(probe, /const float floating = drv\[0\] - sense\[0\];/,
  "The floating reading is what takes the two ADC channels' own offsets out.");

// ---- Checked ADC calibration (application 262163): a DIS string of the sketch's own ----
// One offset calibration in fifty landed about 9 LSB high on board 1, and the wake's battery and
// tip readings carried it. The probe now reads a grounded input after calibrating and calibrates
// again when that zero is out. The text is "z+1 n1 r27 b0"; the scanner decodes exactly that.
assert.match(probe, /snprintf\(g_adcCalText, sizeof\(g_adcCalText\), "z%c%ld n%u r%u b%u", sign, shown,/);
assert.ok(scanner.includes(String.raw`re.fullmatch(r"z([+-]\d+) n(\d) r(\d+) b(\d+)", text)`),
  "The scanner must decode the probe's ADC calibration text and nothing looser.");
// The UUID is written least significant byte first in the sketch.
const adcUuidBytes = probe.match(/UUID128_CHR_ADC_CAL\[16\] = \{([^}]+)\};/)?.[1]
  .split(",").map((b) => Number(b).toString(16).padStart(2, "0")).reverse().join("");
assert.equal(adcUuidBytes?.replace(/^(.{8})(.{4})(.{4})(.{4})(.{12})$/, "$1-$2-$3-$4-$5"),
  scanner.match(/^ADC_CAL_UUID = "([0-9a-f-]+)"$/m)?.[1], "The sketch and the scanner must name the same characteristic.");
assert.equal(Number(probe.match(/constexpr int32_t SAADC_ZERO_MAX_LSB = (\d+);/)?.[1]),
  Number(scanner.match(/^ADC_ZERO_MAX_LSB = (\d+)\b/m)?.[1]));
// From 262164 the window is centred where board 1's good calibrations leave the zero, and the
// scanner judges a build's text against that build's own centre.
assert.equal(Number(probe.match(/constexpr int32_t SAADC_ZERO_CENTRE_LSB = (-?\d+);/)?.[1]),
  Number(scanner.match(/^ADC_ZERO_CENTRE_LSB = (-?\d+)$/m)?.[1]));
assert.match(scanner, /^ADC_ZERO_CENTRED_FROM_VERSION = 262165$/m);
assert.match(probe, /const int32_t off = zero - SAADC_ZERO_CENTRE_LSB;\s*return off <= -SAADC_ZERO_MAX_LSB \|\| off >= SAADC_ZERO_MAX_LSB;/);
assert.match(scanner, /state = "in" if abs\(zero - centre\) < ADC_ZERO_MAX_LSB else "OUT"/);
assert.match(probe, /pinOutput\(PIN_VBAT_DIV_EN, false\);\s*\/\/ the zero: divider off, R9 to ground/,
  "The zero is only a zero with the battery divider switched off.");
assert.match(probe, /saadcCalibrateChecked\(\);\s*float vbatMv = 0;/,
  "Every wake calibrates, checked, before its first reading.");
assert.match(probe, /if \(!zeroRead \|\| !saadcZeroOut\(zero\)\) break;/,
  "A calibration is repeated only on a zero that was read and is out; the last one always stands.");

// ---- Calibration experiment (application 262165): a second DIS string of the sketch's own ----
// Each wake's first calibration is taken one of five ways in rotation; the text counts the high
// ones per way. Whatever way is being tried, a calibration taken again is taken the old way.
assert.match(probe, /snprintf\(g_calExpText, sizeof\(g_calExpText\), "e%s %s %s %s %s w%lu", cell\[0\],/);
assert.match(probe, /if \(g_calArmTimedOut\[i\]\) snprintf\(cell\[i\], sizeof\(cell\[i\]\), "T"\);\s*else snprintf\(cell\[i\], sizeof\(cell\[i\]\), "%u", \(unsigned\)g_calArmHigh\[i\]\);/);
assert.ok(scanner.includes(String.raw`re.fullmatch(r"e(\d+|T) (\d+|T) (\d+|T) (\d+|T) (\d+|T) w(\d+)", text)`),
  "The scanner must decode the probe's experiment text and nothing looser.");
const expUuidBytes = probe.match(/UUID128_CHR_CAL_EXPERIMENT\[16\] = \{([^}]+)\};/)?.[1]
  .split(",").map((b) => Number(b).toString(16).padStart(2, "0")).reverse().join("");
assert.equal(expUuidBytes?.replace(/^(.{8})(.{4})(.{4})(.{4})(.{12})$/, "$1-$2-$3-$4-$5"),
  scanner.match(/^CAL_EXPERIMENT_UUID = "([0-9a-f-]+)"$/m)?.[1], "The sketch and the scanner must name the same characteristic.");
assert.equal(Number(probe.match(/constexpr uint8_t CAL_ARMS = (\d+);/)?.[1]),
  scanner.match(/^CAL_EXPERIMENT_ARMS = \(([^)]*)\)$/m)?.[1].split(",").length,
  "The scanner must name as many ways as the sketch rotates through.");
assert.match(probe, /if \(tries == 0\) saadcCalibrateArm\(arm\);\s*else saadcCalibrate\(\);/,
  "Only a wake's first calibration is the experiment's; a repeat is always the plain one.");

// ---- Battery saver (flags2 bit7, application 262165): the last free bit of the packet ----
// The probe measures moisture less often when its battery is below 3500 mV. The bit says so; the
// scanner decodes it and the receiver ignores it (the readings mean what they always did, there
// are only fewer fresh ones). Saving never loosens the rule above: a measurement wake is
// connectable.
assert.match(probe, /#define ADV_FLAG2_SAVER\s+\(1U << 7\)/);
assert.match(probe, /\(g_powerLevel != PowerLevel::Full \? ADV_FLAG2_SAVER : 0\) \|/);
assert.match(scanner, /^FLAG2_SAVER = 1 << 7$/m);
assert.match(scanner, /"saver": bool\(flags2 & FLAG2_SAVER\),/);
assert.match(probe, /const uint8_t moistureEvery = saverMoistureEveryWakes\(\);\s*if \(g_wakesUntilMoisture > moistureEvery - 1\) g_wakesUntilMoisture = moistureEvery - 1;/,
  "The saver may only ever shorten a wait that is already counted out, never move the wake itself.");
assert.match(probe, /const uint32_t WAKE_PERIOD_MS = 60000;/,
  "The History page and msv4_experimental_moisture.py date a boot by counting one-minute wakes.");

console.log("MSV4 packet ABI: probe, receiver, scanner, History page and cloud function agree.");
