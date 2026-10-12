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
           msv4Rsoil25Ohm, msv4DieTempC, msv4IsOpen, isMsv4Row, msv4SoilCoupledPf,
           msv4RelativeResidualPercent, MSV4_AIR_BASELINES, MSV4_SWEEP_RS_OHM, MSV4_SWEEP_CLOCK_HZ,
           MSV4_SWEEP_DIVISORS };`)();
const row = (fields) => ({ moisture_scale: 4, ...fields });

// ---- Battery: plain curve gives 46 % for 3825 mV; the MSV4 plateau (3820 mV) is 100 % ----
assert.equal(api.msv4BatteryMvToPercent(3825), 100);
assert.equal(api.msv4BatteryMvToPercent(3800), 89);
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

// ---- Experimental reading: soil-coupled capacitance, Cm + Cd - Cd in air ----
// The same cases as self_test() in MSV4/scripts/msv4_experimental_moisture.py.
const word = (cmDeciPf, cdCentiPf) => cmDeciPf + 123 * 0x1000000 + cdCentiPf * 0x100000000;
// 2026-10-10 10:00 PDT is wake 2153 of the boot that began 2026-10-08 22:07:55.
const soilAt = "2026-10-10T17:00:00Z";
const coupled = (fields, sensor = 10) => api.msv4SoilCoupledPf(
  row({ received_at: soilAt, wake: 2153, raw_fdc_counts: 156000, ...fields }), sensor);
// Board 1 in soil, 2026-10-10 10:00 PDT: Cm 28.6 pF, Cd 21.53 pF.
assert.equal(coupled({ bare_tank_fdc_counts: word(286, 2153) }), 36.41);
// Zero in air on this boot's Cp; a little under the air value stays negative.
assert.equal(coupled({ raw_fdc_counts: 4000000, bare_tank_fdc_counts: word(0, 1372) }), 0);
assert.equal(coupled({ raw_fdc_counts: 4000000, bare_tank_fdc_counts: word(0, 1360) }), -0.12);
// An open reading has no Cm, whatever the field holds.
assert.equal(coupled({ raw_fdc_counts: 4000000, bare_tank_fdc_counts: word(5000, 1380) }), 0.08);
// A boot with no entry gets no value: the same moment at wake 10 is a boot nine minutes old.
// This is what keeps a restart nobody recorded from being drawn on the old air value.
assert.equal(coupled({ wake: 10, bare_tank_fdc_counts: word(286, 2153) }), null);
assert.equal(coupled({ wake: null, bare_tank_fdc_counts: word(286, 2153) }), null);
// The match allows for the probe's clock (two wakes out here) and no more (seven).
assert.equal(coupled({ wake: 2155, bare_tank_fdc_counts: word(286, 2153) }), 36.41);
assert.equal(coupled({ wake: 2160, bare_tank_fdc_counts: word(286, 2153) }), null);
// No value before the probe's first entry (wake 168 of the boot of 18:59), for a probe with
// none, with no probe known, or when Cd was not sent.
assert.equal(coupled({ received_at: "2026-10-09T04:46:00Z", wake: 168, raw_fdc_counts: 4000000, bare_tank_fdc_counts: word(0, 1348) }), null);
assert.equal(coupled({ bare_tank_fdc_counts: word(286, 2153) }, 11), null);
assert.equal(coupled({ bare_tank_fdc_counts: word(286, 2153) }, null), null);
assert.equal(coupled({ bare_tank_fdc_counts: word(286, 0) }), null);
// The selector's value is a string.
assert.equal(coupled({ bare_tank_fdc_counts: word(286, 2153) }, "10"), 36.41);
assert.equal(api.msv4SoilCoupledPf({ moisture_scale: 3, received_at: soilAt, wake: 2153, bare_tank_fdc_counts: 22480000 }, 10), null);
assert.match(page, /"Msv4Coupled", historySeries\(\(r\) => msv4SoilCoupledPf\(r, lastHistorySensor\)\)/,
  "The experimental card must be drawn for the probe its rows were loaded for.");
assert.match(page, /lastHistoryRows = rows;\s*lastHistorySensor = Number\(sensor\);/);

// The page and the analysis script must hold the same air values.
const script = await readFile(join(
  displayCenterRoot, "..", "MSV4", "scripts", "msv4_experimental_moisture.py",
), "utf8");
const scriptBaselines = [...script.matchAll(/^\s*\((\d+), "([^"]+)", ([\d.]+), ([\d.]+),$/gm)]
  .map((m) => ({ sensor: Number(m[1]), boot: m[2], cdAirPf: Number(m[3]), cpPf: Number(m[4]) }));
const scriptNumber = (name) => Number(script.match(new RegExp(`^${name} = ([\\d.]+)$`, "m"))?.[1]);
assert.equal(scriptNumber("WAKE_PERIOD_S") * 1000, Number(page.match(/const MSV4_WAKE_PERIOD_MS = (\d+);/)?.[1]));
assert.equal(scriptNumber("BOOT_MATCH_S") * 1000, Number(page.match(/const MSV4_BOOT_MATCH_MS = (\d+);/)?.[1]));
assert.equal(scriptNumber("BOOT_MATCH_PER_UPTIME"), Number(page.match(/const MSV4_BOOT_MATCH_PER_UPTIME = ([\d.]+);/)?.[1]));
assert.ok(scriptBaselines.length > 0, "Could not find AIR_BASELINES in msv4_experimental_moisture.py");
assert.deepEqual(api.MSV4_AIR_BASELINES, scriptBaselines,
  "MSV4_AIR_BASELINES in MobileAppPage.h and AIR_BASELINES in msv4_experimental_moisture.py differ.");

// ---- Fit residual as a share of the electrode signal ----
// Board 1's own readings of 2026-10-10, the same cases and answers as the script's self-test:
// dry soil before the watering, the first reading with water on the blade, 40 minutes later, air.
const fitWord = (cmDeciPf, cdCentiPf, residualE4) => cmDeciPf + residualE4 * 0x1000000 + cdCentiPf * 0x100000000;
const share = (rsoilDeci, cmDeciPf, cdCentiPf, residualE4, extra = {}) => api.msv4RelativeResidualPercent(
  row({ received_at: soilAt, wake: 2153, raw_fdc_counts: rsoilDeci, bare_tank_fdc_counts: fitWord(cmDeciPf, cdCentiPf, residualE4), ...extra }), 10);
assert.equal(share(153200, 301, 2151, 129), 7.2);
assert.equal(share(10770, 3216, 7688, 201), 3.59);
assert.equal(share(27530, 1243, 3596, 141), 4.12);
assert.equal(share(4000000, 0, 1372, 228), 20.5);
// A boot with no entry has no Cp on record, so no value; nor a row with no Cd.
assert.equal(share(27530, 1243, 3596, 141, { wake: 10 }), null);
assert.equal(share(27530, 1243, 0, 141), null);
assert.match(page, /"Msv4RelativeResidual", historySeries\(\(r\) => msv4RelativeResidualPercent\(r, lastHistorySensor\)\)/);
// The raw residual (a share of the drive) has no card of its own since 2026-10-10: it does not
// compare between dry and wet. A script lookup of a section that is no longer in the page throws.
assert.doesNotMatch(page, /historyMsv4ResidualSection|"Msv4Residual"/,
  "The raw fit-residual card was removed; every reference to it has to go with it.");
// The sweep the page assumes is the one the probe runs and the script assumes.
const sketch = await readFile(join(
  displayCenterRoot, "..", "MSV4", "arduino", "MSV4_Moisture_BLE_Sleep", "MSV4_Moisture_BLE_Sleep.ino",
), "utf8");
assert.equal(api.MSV4_SWEEP_RS_OHM, Number(sketch.match(/constexpr double SPEC_RS = ([\d.]+);/)?.[1]));
assert.equal(api.MSV4_SWEEP_CLOCK_HZ, Number(sketch.match(/constexpr uint32_t FE_CLOCK_HZ = (\d+)UL;/)?.[1]));
assert.deepEqual(api.MSV4_SWEEP_DIVISORS,
  sketch.match(/static const uint16_t SPEC_N\[\] = \{([^}]+)\};/)?.[1].split(",").map(Number));
assert.equal(api.MSV4_SWEEP_RS_OHM, scriptNumber("SWEEP_RS_OHM"));
assert.equal(api.MSV4_SWEEP_CLOCK_HZ, scriptNumber("SWEEP_CLOCK_HZ"));
assert.deepEqual(api.MSV4_SWEEP_DIVISORS,
  script.match(/^SWEEP_DIVISORS = \(([^)]+)\)$/m)?.[1].split(",").map(Number));

console.log("PASS MSV4 History decoding: battery window, packed diagnostics, open state, die temperature, the experimental soil-coupled capacitance and the signal-relative residual");
