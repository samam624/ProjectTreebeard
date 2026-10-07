import assert from "node:assert/strict";
import test from "node:test";

import {
  MSV3_FDC_COUNT_MAX,
  MSV4_MOISTURE_SCALE,
  packMsv4Diagnostics,
  parseMsv3Diagnostics,
  parseMsv4DieTempQuarterC,
} from "./msv3-diagnostics.ts";

test("a valid negative q4 temperature survives unchanged", () => {
  const result = parseMsv3Diagnostics({ boardTempQuarterC: -40, bareTankFdcCounts: 1000, tankPhase: 0 });
  assert.equal(result.boardTempQuarterC, -40);
});

test("an absent extension (no raw object at all) yields nulls, not zeros", () => {
  const result = parseMsv3Diagnostics(undefined);
  assert.deepEqual(result, { boardTempQuarterC: null, bareTankFdcCounts: null, tankPhase: null });
});

test("an out-of-range temperature is rejected to null, not clamped", () => {
  assert.equal(parseMsv3Diagnostics({ boardTempQuarterC: 341 }).boardTempQuarterC, null);
  assert.equal(parseMsv3Diagnostics({ boardTempQuarterC: -161 }).boardTempQuarterC, null);
  assert.equal(parseMsv3Diagnostics({ boardTempQuarterC: 340 }).boardTempQuarterC, 340);
  assert.equal(parseMsv3Diagnostics({ boardTempQuarterC: -160 }).boardTempQuarterC, -160);
  assert.equal(parseMsv3Diagnostics({ boardTempQuarterC: 12.5 }).boardTempQuarterC, null);
});

test("phase is restricted to exactly 0 or 1", () => {
  assert.equal(parseMsv3Diagnostics({ tankPhase: 0 }).tankPhase, 0);
  assert.equal(parseMsv3Diagnostics({ tankPhase: 1 }).tankPhase, 1);
  assert.equal(parseMsv3Diagnostics({ tankPhase: 2 }).tankPhase, null);
  assert.equal(parseMsv3Diagnostics({ tankPhase: -1 }).tankPhase, null);
});

test("bare-tank count is bounded to a non-negative 28-bit FDC value", () => {
  assert.equal(parseMsv3Diagnostics({ bareTankFdcCounts: -1 }).bareTankFdcCounts, null);
  assert.equal(parseMsv3Diagnostics({ bareTankFdcCounts: MSV3_FDC_COUNT_MAX }).bareTankFdcCounts, MSV3_FDC_COUNT_MAX);
  assert.equal(parseMsv3Diagnostics({ bareTankFdcCounts: MSV3_FDC_COUNT_MAX + 1 }).bareTankFdcCounts, null);
  assert.equal(parseMsv3Diagnostics({ bareTankFdcCounts: 20340000 }).bareTankFdcCounts, 20340000);
});

test("one invalid field does not invalidate its valid siblings", () => {
  const result = parseMsv3Diagnostics({ boardTempQuarterC: 999, bareTankFdcCounts: 20340000, tankPhase: 1 });
  assert.equal(result.boardTempQuarterC, null);
  assert.equal(result.bareTankFdcCounts, 20340000);
  assert.equal(result.tankPhase, 1);
});

test("batch values survive into the RPC input object (mirrors index.ts's readings.push shape)", () => {
  // index.ts spreads parseMsv3Diagnostics(plant?.raw) directly into each batch reading row
  // alongside the other existing fields - this confirms that construction pattern actually
  // carries the values through, not just that the parser itself is correct in isolation.
  const plant = {
    sensor: 4,
    observationId: "boot1-9",
    raw: { boardTempQuarterC: 84, bareTankFdcCounts: 22620000, tankPhase: 1 },
  };
  const row = {
    sensorId: plant.sensor,
    observationId: plant.observationId,
    ...parseMsv3Diagnostics(plant.raw),
  };
  assert.deepEqual(row, {
    sensorId: 4,
    observationId: "boot1-9",
    boardTempQuarterC: 84,
    bareTankFdcCounts: 22620000,
    tankPhase: 1,
  });
});

test("MSV4 fit diagnostics pack into one word", () => {
  // Application 262148/262150: Cm 671.0 pF, Cp 8.8 pF, residual 2.29 %.
  const packed = packMsv4Diagnostics({ cmDeciPf: 6710, cpDeciPf: 88, fitRmsE4: 229 });
  assert.equal(packed, 0xE5581A36);
  assert.ok(packed > MSV3_FDC_COUNT_MAX, "the word needs more than the 28-bit FDC bound");
  // A residual with its top bit set must stay positive (no 32-bit signed arithmetic).
  assert.equal(packMsv4Diagnostics({ cmDeciPf: 65535, cpDeciPf: 255, fitRmsE4: 255 }), 0xFFFFFFFF);
});

test("MSV4 Cd sits above bit 32 at 0.01 pF and Cp may be absent", () => {
  // Application 262152, board 1 in air through the console: Cm 0, residual 2.29 %, Cd 13.01 pF.
  const air = packMsv4Diagnostics({ cmDeciPf: 0, fitRmsE4: 229, cdCentiPf: 1301 });
  assert.equal(air, 229 * 0x1000000 + 1301 * 0x100000000);
  assert.ok(Number.isSafeInteger(air));
  assert.equal(
    packMsv4Diagnostics({ cmDeciPf: 65535, cpDeciPf: 255, fitRmsE4: 255, cdCentiPf: 65535 }), 2 ** 48 - 1);
  // The low 32 bits keep their meaning, so rows stored before Cd existed decode unchanged.
  const withCd = packMsv4Diagnostics({ cmDeciPf: 6710, cpDeciPf: 88, fitRmsE4: 229, cdCentiPf: 1301 });
  assert.equal(withCd % 0x100000000, 0xE5581A36);
  // Zero, out-of-range or null Cd and Cp are "not sent", not errors.
  const base = 6710 + 15 * 0x1000000;
  assert.equal(packMsv4Diagnostics({ cmDeciPf: 6710, fitRmsE4: 15 }), base);
  assert.equal(packMsv4Diagnostics({ cmDeciPf: 6710, fitRmsE4: 15, cdCentiPf: 0, cpDeciPf: 0 }), base);
  assert.equal(packMsv4Diagnostics({ cmDeciPf: 6710, fitRmsE4: 15, cdCentiPf: 65536, cpDeciPf: 256 }), base);
  assert.equal(packMsv4Diagnostics({ cmDeciPf: 6710, fitRmsE4: 15, cdCentiPf: null, cpDeciPf: null }), base);
});

test("MSV4 diagnostics need Cm and the residual", () => {
  assert.equal(packMsv4Diagnostics(undefined), null);
  assert.equal(packMsv4Diagnostics({ cmDeciPf: 6710, cpDeciPf: 88 }), null);
  assert.equal(packMsv4Diagnostics({ fitRmsE4: 229, cdCentiPf: 1301 }), null);
  assert.equal(packMsv4Diagnostics({ cmDeciPf: 65536, fitRmsE4: 1 }), null);
  assert.equal(packMsv4Diagnostics({ cmDeciPf: 1, fitRmsE4: -1 }), null);
  assert.equal(packMsv4Diagnostics({ cmDeciPf: 1.5, fitRmsE4: 1 }), null);
  // Number(null) is 0: an explicit null must not become a zero reading.
  assert.equal(packMsv4Diagnostics({ cmDeciPf: null, fitRmsE4: 228 }), null);
});

test("the moisture scale decides what the bare-tank column means", () => {
  const raw = {
    boardTempQuarterC: 95,
    tankPhase: 1,
    bareTankFdcCounts: 22480000,
    msv4: { cmDeciPf: 0, cpDeciPf: 88, fitRmsE4: 230 },
  };
  // Scale 4: the packed word, never the tank count; tip temperature and phase pass through.
  assert.deepEqual(parseMsv3Diagnostics(raw, MSV4_MOISTURE_SCALE), {
    boardTempQuarterC: 95,
    bareTankFdcCounts: 88 * 65536 + 230 * 16777216,
    tankPhase: 1,
  });
  // An MSV4 row without the diagnostics stores null, not a stray tank count.
  assert.equal(parseMsv3Diagnostics({ bareTankFdcCounts: 22480000 }, 4).bareTankFdcCounts, null);
  // Every other scale, and a caller that passes none, behaves exactly as before.
  assert.equal(parseMsv3Diagnostics(raw, 3).bareTankFdcCounts, 22480000);
  assert.equal(parseMsv3Diagnostics(raw).bareTankFdcCounts, 22480000);
  assert.equal(parseMsv3Diagnostics({ bareTankFdcCounts: MSV3_FDC_COUNT_MAX + 1 }, 2).bareTankFdcCounts, null);
});

test("MSV4 die temperature is range-checked and never invented from null", () => {
  assert.equal(parseMsv4DieTempQuarterC({ dieTempQuarterC: 126 }), 126);
  assert.equal(parseMsv4DieTempQuarterC({ dieTempQuarterC: -160 }), -160);
  assert.equal(parseMsv4DieTempQuarterC({ dieTempQuarterC: 0 }), 0);
  assert.equal(parseMsv4DieTempQuarterC({ dieTempQuarterC: 341 }), null);
  assert.equal(parseMsv4DieTempQuarterC({ dieTempQuarterC: null }), null);
  assert.equal(parseMsv4DieTempQuarterC({ dieTempQuarterC: "126" }), null);
  assert.equal(parseMsv4DieTempQuarterC({}), null);
  assert.equal(parseMsv4DieTempQuarterC(undefined), null);
  // A 23-byte packet (application 262148) has diagnostics but no die temperature.
  assert.equal(parseMsv4DieTempQuarterC({ cmDeciPf: 0, cpDeciPf: 88, fitRmsE4: 228 }), null);
});
