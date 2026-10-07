// Runs the History page's own MSV4 decoding against synthetic rows.
//
// verify_msv4_packet_abi.mjs proves the source texts agree; this proves the arithmetic. An MSV4
// history row has no fields of its own: it reuses four MSV3 columns, two of them as packed words,
// and the page is the only place they are unpacked for a person to read. The functions are lifted
// out of MobileAppPage.h and executed, so this fails if they stop decoding these rows correctly.
//
// The reference numbers are measurements from board 1 on 2026-10-06 where one exists.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const displayCenterRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const page = await readFile(join(
  displayCenterRoot, "arduino", "Treebeard_ESP32_Display_Runtime", "MobileAppPage.h",
), "utf8");

const start = page.indexOf("const BATTERY_CURVE = [");
const end = page.indexOf("// Faithful port of calibrateRawBattery()");
assert.ok(start > 0 && end > start, "Could not find the battery/MSV4 helper block in MobileAppPage.h");
const api = new Function(`${page.slice(start, end)};
  return { batteryMvToPercent, msv4BatteryMvToPercent, msv4Diagnostics, msv4RsoilOhm,
           msv4Rsoil25Ohm, msv4DieTempC, msv4IsOpen, isMsv4Row };`)();
const row = (fields) => ({ moisture_scale: 4, ...fields });

// ---- Battery: the receiver logged 74 % for 3825 mV; the plain curve gives 46 % ----
assert.equal(api.msv4BatteryMvToPercent(3825), 74);
assert.equal(api.batteryMvToPercent(3825), 46);
assert.equal(api.msv4BatteryMvToPercent(3889), 100);
assert.equal(api.msv4BatteryMvToPercent(3369), 0);
assert.equal(api.msv4BatteryMvToPercent(4200), 100);
assert.equal(api.msv4BatteryMvToPercent(3000), 0);

// ---- The packed diagnostics word ----
// Probe application 262152, board 1 in air through the console: Cm 0, residual 2.29 %,
// Cd 13.01 pF, and no Cp (that firmware does not send it).
const air = 229 * 0x1000000 + 1301 * 0x100000000;
assert.deepEqual(api.msv4Diagnostics(row({ bare_tank_fdc_counts: air })),
  { cmPf: 0, cpPf: null, residualPercent: 2.29, cdPf: 13.01 });
// A row stored by application 262148/262150 (32-bit word): Cp present, Cd absent.
assert.deepEqual(api.msv4Diagnostics(row({ bare_tank_fdc_counts: 0xE5581A36 })),
  { cmPf: 671, cpPf: 8.8, residualPercent: 2.29, cdPf: null });
assert.deepEqual(api.msv4Diagnostics(row({ bare_tank_fdc_counts: 2 ** 48 - 1 })),
  { cmPf: 6553.5, cpPf: 25.5, residualPercent: 2.55, cdPf: 655.35 });
assert.equal(api.msv4Diagnostics(row({ bare_tank_fdc_counts: null })), null);
assert.equal(api.msv4Diagnostics(row({ bare_tank_fdc_counts: 2 ** 48 })), null);
// The same column on an MSV3 row is a tank count and must not be unpacked.
assert.equal(api.msv4Diagnostics({ moisture_scale: 3, bare_tank_fdc_counts: 22480000 }), null);

// ---- Resistance ----
assert.equal(api.msv4RsoilOhm(row({ raw_fdc_counts: 3550 })), 355);
// 26.75 C is 1.75 C above reference: +3.5 %.
assert.ok(Math.abs(api.msv4Rsoil25Ohm(row({ raw_fdc_counts: 3550, board_temp_quarter_c: 107 })) - 355 * 1.035) < 1e-9);
assert.equal(api.msv4Rsoil25Ohm(row({ raw_fdc_counts: 3550, board_temp_quarter_c: null })), null);
assert.equal(api.msv4RsoilOhm(row({ raw_fdc_counts: 4000001 })), null);
assert.equal(api.msv4RsoilOhm({ moisture_scale: 3, raw_fdc_counts: 39400000 }), null);

// ---- Open circuit is a state, never a 400 kohm data point ----
const open = row({ raw_fdc_counts: 4000000, board_temp_quarter_c: 93, bare_tank_fdc_counts: air });
assert.equal(api.msv4IsOpen(open), true);
assert.equal(api.msv4Rsoil25Ohm(open), null);
assert.equal(api.msv4IsOpen(row({ raw_fdc_counts: 3550 })), false);
assert.equal(api.msv4IsOpen({ moisture_scale: 3, raw_fdc_counts: 39400000 }), false);
assert.match(page, /historySeries\(\(r\) => \(msv4IsOpen\(r\) \? null : msv4RsoilOhm\(r\)\)\)/,
  "The resistance card must skip open readings instead of plotting the ceiling.");
assert.match(page, /msv4IsOpen\(r\) \? null : msv4Diagnostics\(r\)\?\.cmPf/,
  "Cm is undefined while nothing conducts and must not be plotted for open readings.");

// ---- Die temperature (raw_moisture_khz on an MSV4 row) ----
assert.equal(api.msv4DieTempC(row({ raw_moisture_khz: 94 })), 23.5);
assert.equal(api.msv4DieTempC(row({ raw_moisture_khz: -22 })), -5.5);
assert.equal(api.msv4DieTempC(row({ raw_moisture_khz: null })), null);
assert.equal(api.msv4DieTempC(row({ raw_moisture_khz: 341 })), null);
// On an MSV2 row that column really is an oscillator frequency.
assert.equal(api.msv4DieTempC({ moisture_scale: 1, raw_moisture_khz: 94 }), null);

console.log("PASS MSV4 History decoding: battery window, packed diagnostics, open state and die temperature");
