import assert from "node:assert/strict";
import test from "node:test";

import { parseMsv3Diagnostics, MSV3_FDC_COUNT_MAX } from "./msv3-diagnostics.ts";

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
