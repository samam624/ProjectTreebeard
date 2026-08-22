import assert from "node:assert/strict";
import test from "node:test";

import { deriveAutoCal, type AutoCalRawSensor } from "./autocal.ts";

const DAY_MS = 24 * 3600_000;
const NOW_MS = Date.parse("2026-08-22T06:20:00Z");

function sensor(overrides: Partial<AutoCalRawSensor>): AutoCalRawSensor {
  return {
    sensorId: 2,
    sampleCount: 1000,
    oldestReadingAt: new Date(NOW_MS - 10 * DAY_MS).toISOString(),
    dryBound: 163,
    wetBound: 177,
    rises: [],
    ...overrides,
  };
}

test("blends one supported watering with the continuously updated time-weighted prior", () => {
  const result = deriveAutoCal(sensor({
    rises: [{
      at: "2026-08-17T01:05:00Z",
      delta: 15,
      dryBound: 166,
      wetBound: 181,
      preSampleCount: 23,
      postSampleCount: 35,
      sustainedDelta: 12,
    }],
  }), NOW_MS);

  assert.equal(result.ready, true);
  assert.equal(result.dryBound, 166);
  assert.equal(result.wetBound, 180);
  assert.equal(result.calibrationSource, "adaptive-watering-history");
  assert.equal(result.wateringCycleCount, 1);
  assert.equal(result.calibrationConfidence, 39);
});

test("multiple watering cycles all influence the learned dry point and span", () => {
  const result = deriveAutoCal(sensor({
    rises: [
      { at: "2026-08-09T08:15:00Z", delta: 27, dryBound: 159, wetBound: 186,
        preSampleCount: 24, postSampleCount: 37, sustainedDelta: 14 },
      { at: "2026-08-17T01:05:00Z", delta: 15, dryBound: 166, wetBound: 181,
        preSampleCount: 23, postSampleCount: 35, sustainedDelta: 12 },
    ],
  }), NOW_MS);

  assert.equal(result.dryBound, 164);
  assert.equal(result.wetBound, 182);
  assert.equal(result.wateringCycleCount, 2);
  assert.ok(result.dryBound! < 166, "older dry evidence must affect the result");
  assert.ok(result.wetBound! > 181, "older wetting-span evidence must affect the result");
});

test("independently scaled shared-pot probes no longer resolve to 31% versus 0%", () => {
  const probe2 = deriveAutoCal(sensor({
    sensorId: 2,
    rises: [
      { at: "2026-08-09T08:15:00Z", delta: 27, dryBound: 159, wetBound: 186,
        preSampleCount: 24, postSampleCount: 37, sustainedDelta: 14 },
      { at: "2026-08-17T01:05:00Z", delta: 15, dryBound: 166, wetBound: 181,
        preSampleCount: 23, postSampleCount: 35, sustainedDelta: 12 },
    ],
  }), NOW_MS);
  const probe3 = deriveAutoCal(sensor({
    sensorId: 3,
    dryBound: 909,
    wetBound: 1235,
    // The August 9 probe-3 plunge/recovery is rejected by migration 025's sustained
    // context test, leaving the real simultaneous August 17 watering.
    rises: [{ at: "2026-08-17T01:05:00Z", delta: 431, dryBound: 884, wetBound: 1317,
      preSampleCount: 23, postSampleCount: 35, sustainedDelta: 367 }],
  }), NOW_MS);

  const percent = (value: number, dry: number, wet: number) =>
    Math.round(100 * (value - dry) / (wet - dry));
  const probe2Percent = percent(170, probe2.dryBound!, probe2.wetBound!);
  const probe3Percent = percent(980, probe3.dryBound!, probe3.wetBound!);

  assert.equal(probe2Percent, 33);
  assert.equal(probe3Percent, 22);
  assert.ok(Math.abs(probe2Percent - probe3Percent) <= 12);
});

test("clusters a multi-step watering and keeps the first dry point plus highest wet peak", () => {
  const result = deriveAutoCal(sensor({
    rises: [
      { at: "2026-08-17T01:05:00Z", delta: 10, dryBound: 166, wetBound: 176 },
      { at: "2026-08-17T02:05:00Z", delta: 5, dryBound: 176, wetBound: 181 },
    ],
  }), NOW_MS);

  assert.equal(result.dryBound, 166);
  assert.equal(result.wetBound, 180);
  assert.equal(result.wateringCycleCount, 1);
});

test("a single extreme cycle cannot take over once three supported cycles exist", () => {
  const result = deriveAutoCal(sensor({
    dryBound: 100,
    wetBound: 200,
    rises: [
      { at: "2026-08-04T01:00:00Z", delta: 100, dryBound: 100, wetBound: 200 },
      { at: "2026-08-12T01:00:00Z", delta: 100, dryBound: 102, wetBound: 202 },
      { at: "2026-08-21T01:00:00Z", delta: 400, dryBound: 500, wetBound: 900 },
    ],
  }), NOW_MS);

  assert.ok(result.dryBound! < 120);
  assert.ok(result.wetBound! < 230);
  assert.equal(result.wateringCycleCount, 3);
});

test("new non-watering buckets can refine the weak prior between watering events", () => {
  const rise = [{ at: "2026-08-17T01:05:00Z", delta: 15, dryBound: 166, wetBound: 181 }];
  const first = deriveAutoCal(sensor({ dryBound: 163, wetBound: 177, rises: rise }), NOW_MS);
  const updated = deriveAutoCal(sensor({ dryBound: 150, wetBound: 195, rises: rise }), NOW_MS);

  assert.notDeepEqual(
    [first.dryBound, first.wetBound],
    [updated.dryBound, updated.wetBound],
  );
});

test("a new watering contributes provisionally but cannot outweigh confirmed history yet", () => {
  const confirmedRise = {
    at: "2026-08-12T01:00:00Z", delta: 100, dryBound: 100, wetBound: 200,
    preSampleCount: 24, postSampleCount: 36, laterSampleCount: 72,
    persistenceValidated: true,
  };
  const newRise = {
    at: "2026-08-22T01:00:00Z", delta: 200, dryBound: 200, wetBound: 400,
    preSampleCount: 24, postSampleCount: 18, laterSampleCount: 0,
    persistenceValidated: false,
  };
  const provisional = deriveAutoCal(sensor({
    dryBound: 100,
    wetBound: 200,
    rises: [confirmedRise, newRise],
  }), NOW_MS);
  const prematurelyTrusted = deriveAutoCal(sensor({
    dryBound: 100,
    wetBound: 200,
    rises: [confirmedRise, { ...newRise, laterSampleCount: 72, persistenceValidated: true }],
  }), NOW_MS);

  assert.ok(provisional.dryBound! < prematurelyTrusted.dryBound!);
  assert.equal(provisional.provisionalWateringCycleCount, 1);
  assert.equal(prematurelyTrusted.provisionalWateringCycleCount, 0);
});

test("falls back to time-weighted percentiles before cycle landmarks are available", () => {
  const result = deriveAutoCal(sensor({ rises: [] }), NOW_MS);

  assert.equal(result.dryBound, 163);
  assert.equal(result.wetBound, 177);
  assert.equal(result.calibrationSource, "time-weighted-percentiles");
});

test("does not trust learned bounds before the three-day readiness gate", () => {
  const result = deriveAutoCal(sensor({
    oldestReadingAt: new Date(NOW_MS - 2 * DAY_MS).toISOString(),
    rises: [{ at: "2026-08-21T01:05:00Z", delta: 15, dryBound: 166, wetBound: 181 }],
  }), NOW_MS);

  assert.equal(result.ready, false);
  assert.equal(result.dryBound, 163);
  assert.equal(result.wetBound, 177);
});
