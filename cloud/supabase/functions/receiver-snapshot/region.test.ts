import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

import { deriveRegion, REGION, REGION_OVERDUE_PERCENT, type RegionRawSensor } from "./region.ts";

const HOUR_MS = 3600_000;

// The Alocasia pot's sensor 3, 2026-08-09 to 2026-09-30, as hourly medians: seven waterings the
// maintainer judged (five on time, the fertilized sixth a little early), one knocked probe, one
// fertilizing. cloud/scripts/region_replay.mjs --fixture wrote it; water_now_replay.py is the
// Python reference the hours below were checked against.
const alocasia = JSON.parse(
  readFileSync(new URL("./fixtures/region-alocasia-sensor3-hourly.json", import.meta.url), "utf8"),
) as RegionRawSensor;

function at(sensor: RegionRawSensor, iso: string) {
  return deriveRegion(sensor, Date.parse(iso));
}

// What the display would have said in the hour before `iso`, and for how many hours running.
function saidBefore(sensor: RegionRawSensor, iso: string): { said: string; hours: number } {
  const end = Math.floor(Date.parse(iso) / HOUR_MS);
  const said = (hour: number): string => {
    const row = sensor.hours.find((entry) => entry[0] === hour);
    if (!row) return "no data";
    const region = deriveRegion(sensor, (hour + 1) * HOUR_MS);
    return !region.ready ? "-" : row[1] < region.bottom! ? "overdue" : row[1] <= region.top! ? "water now" : "not yet";
  };
  let hour = end - 1;
  while (said(hour) === "no data" && hour > end - 12) hour--;
  const last = said(hour);
  let run = 0;
  for (let k = end - 1; k > end - 96; k--) {
    const state = said(k);
    if (state === "no data") continue;
    if (state !== "water now" && state !== "overdue") break;
    run++;
  }
  return { said: last, hours: run };
}

test("overdue lands on one fixed percent of the scale the receiver draws", () => {
  assert.equal(REGION_OVERDUE_PERCENT, -21);
});

test("learns the watering line from the first two waterings and then stops", () => {
  const afterOne = at(alocasia, "2026-08-18T12:00:00Z");
  assert.equal(afterOne.ready, true);
  assert.equal(afterOne.taught, 1);
  assert.equal(afterOne.anchorToStore, null);

  const afterTwo = at(alocasia, "2026-08-26T12:00:00Z");
  assert.equal(afterTwo.taught, 2);
  assert.ok(Math.abs(afterTwo.anchorToStore! - 0.673) < 0.005, `anchor ${afterTwo.anchorToStore}`);

  // Five more waterings, one of them early and fertilized, do not move it.
  const atEnd = at(alocasia, "2026-10-01T00:00:00Z");
  assert.equal(atEnd.drop, afterTwo.drop);
});

test("says water now before each on-time watering and not yet before the early one", () => {
  // Hours the Python reference had it on for: 21, 34, 15 and 24 (waterings 2, 4, 5 and 7).
  const second = saidBefore(alocasia, "2026-08-24T19:35:00Z");
  assert.equal(second.said, "water now");
  assert.ok(second.hours >= 15 && second.hours <= 24, `watering 2: ${second.hours} h`);

  const fourth = saidBefore(alocasia, "2026-09-07T20:20:00Z");
  assert.equal(fourth.said, "water now");
  assert.ok(fourth.hours >= 30 && fourth.hours <= 38, `watering 4: ${fourth.hours} h`);

  const fifth = saidBefore(alocasia, "2026-09-15T01:40:00Z");
  assert.equal(fifth.said, "water now");
  assert.ok(fifth.hours >= 8 && fifth.hours <= 18, `watering 5: ${fifth.hours} h`);

  const fertilized = saidBefore(alocasia, "2026-09-22T17:50:00Z");
  assert.equal(fertilized.said, "not yet");

  const seventh = saidBefore(alocasia, "2026-09-30T18:15:00Z");
  assert.equal(seventh.said, "water now");
  assert.ok(seventh.hours >= 20 && seventh.hours <= 28, `watering 7: ${seventh.hours} h`);
});

test("never says overdue on a record where every watering was on time or early", () => {
  for (const [hour, reading] of alocasia.hours) {
    const region = deriveRegion(alocasia, (hour + 1) * HOUR_MS);
    if (region.ready) assert.ok(reading >= region.bottom!, `overdue at hour ${hour}`);
  }
});

test("a knocked probe gets no region until the next watering, and teaches nothing", () => {
  const knocked = at(alocasia, "2026-08-29T06:00:00Z");
  assert.equal(knocked.ready, false);
  assert.equal(knocked.disturbed, true);

  const settledAgain = at(alocasia, "2026-08-31T12:00:00Z");
  assert.equal(settledAgain.ready, true);
  assert.equal(settledAgain.disturbed, false);
  assert.equal(settledAgain.waterings[2].taughtDrop, null);
  assert.match(settledAgain.waterings[2].verdict, /new scale after a disturbance/);
});

test("a fertilized watering is held as a shift for a day and then moves the region with the scale", () => {
  const held = at(alocasia, "2026-09-23T23:00:00Z");
  assert.equal(held.heldAsShift, true);
  // Moved up by the whole step: the region sits where the old one was plus (1381 - 1084).
  assert.ok(held.top! > 1100, `held top ${held.top}`);

  const judged = at(alocasia, "2026-09-25T12:00:00Z");
  assert.equal(judged.heldAsShift, false);
  assert.match(judged.waterings[5].verdict, /change of scale/);
  assert.ok(Math.abs(judged.top! - 976) <= 4, `rescaled top ${judged.top}`);
  assert.ok(Math.abs(judged.bottom! - 890) <= 4, `rescaled bottom ${judged.bottom}`);
  assert.ok(Math.abs(judged.settled! - 1381) <= 3);
});

test("a stored anchor is used as it stands and nothing is offered for storing", () => {
  const stored = at({ ...alocasia, regionDrop: 0.6, regionMode: "share" }, "2026-09-20T12:00:00Z");
  assert.equal(stored.drop, 0.6);
  assert.equal(stored.anchorStored, true);
  assert.equal(stored.anchorToStore, null);
  // settled 1084, base 730: top = 730 + (1 - 0.925 * 0.6) * 354
  assert.ok(Math.abs(stored.top! - Math.round(730 + (1 - REGION.TOP * 0.6) * (stored.settled! - 730))) <= 1);
});

// ---- synthetic records: [hour, reading] built from a list of (hours, level) stretches
function record(...stretches: [number, number | ((hour: number) => number)][]): [number, number][] {
  const out: [number, number][] = [];
  let hour = 500_000;
  for (const [length, level] of stretches) {
    for (let k = 0; k < length; k++) out.push([hour++, Math.round(typeof level === "function" ? level(k) : level)]);
  }
  return out;
}
const endOf = (hours: [number, number][]) => (hours[hours.length - 1][0] + 1) * HOUR_MS;

test("MSV4: going into soil is not a watering; the first watering teaches a span", () => {
  const hours = record(
    [20, 0],                                   // in air: nothing conducting
    [45, (k) => 13000 + Math.min(k, 10) * 140], // into dry soil, closing on the blade
    [30, (k) => 21500 - k * 12],               // watered
  );
  const region = deriveRegion({ sensorId: 10, scale: 4, hours }, endOf(hours));
  assert.equal(region.mode, "span");
  assert.equal(region.waterings.length, 1);
  assert.ok(Math.abs(region.waterings[0].levelBefore - 14400) < 50);
  assert.equal(region.ready, true);
  assert.equal(region.taught, 1);
  const drop = region.settled! - 14400;
  assert.ok(Math.abs(region.top! - (region.settled! - REGION.TOP * drop)) <= 2);
  assert.ok(Math.abs(region.bottom! - (region.settled! - REGION.BOTTOM * drop)) <= 2);
});

test("MSV4: more ions slide the region up with the settled level; the span does not stretch", () => {
  const dryDown = (from: number) => (k: number) => from - k * 40;
  const hours = record(
    [30, 14000], [170, dryDown(21000)],        // watering 1 at 14000, settles ~20400
    [170, dryDown(21000)],                     // watering 2 at ~14240
    [60, dryDown(24000)],                      // watering 3, fertilized: +3000 on a log scale
  );
  const before = deriveRegion({ sensorId: 10, scale: 4, hours: hours.slice(0, 360) }, (hours[359][0] + 1) * HOUR_MS);
  const after = deriveRegion({ sensorId: 10, scale: 4, hours }, endOf(hours));
  assert.equal(after.taught, 2);
  assert.equal(after.heldAsShift, false);
  const moved = after.settled! - before.settled!;
  assert.ok(Math.abs(moved - 3000) < 60, `settled moved ${moved}`);
  assert.ok(Math.abs((after.top! - before.top!) - moved) <= 2);
  assert.ok(Math.abs((after.bottom! - before.bottom!) - moved) <= 2);
});

test("a small watering takes only half of the fall, so the region cannot drop out from under a dry pot", () => {
  const dryDown = (from: number) => (k: number) => from - k * 40;
  const hours = record(
    [30, 14000], [170, dryDown(21000)],
    [170, dryDown(21000)],
    [60, dryDown(17500)],                      // a half watering
  );
  const full = deriveRegion({ sensorId: 10, scale: 4, hours: hours.slice(0, 360) }, (hours[359][0] + 1) * HOUR_MS);
  const small = deriveRegion({ sensorId: 10, scale: 4, hours }, endOf(hours));
  const fall = full.settled! - (17500 - 18 * 40);
  assert.ok(Math.abs((full.top! - small.top!) - REGION.LEAK * fall) <= 40, `top moved ${full.top! - small.top!} of a ${fall} fall`);
  assert.match(small.waterings[2].verdict, /half of the fall taken/);
});

test("nothing is ready before a watering has settled", () => {
  const hours = record([40, 14000], [10, 21000]);
  const region = deriveRegion({ sensorId: 10, scale: 4, hours }, endOf(hours));
  assert.equal(region.ready, false);
  assert.equal(region.waterings.length, 1);
  assert.match(region.reason, /settled/);
});

test("the receiver's row carries the region in the names it has always read", async () => {
  const { regionAutoCalRow } = await import("./region.ts");
  const ready = regionAutoCalRow(at(alocasia, "2026-09-20T12:00:00Z"));
  assert.deepEqual(Object.keys(ready), ["sensorId", "ready", "dryBound", "wetBound", "daysSinceLastWatering", "lastWateredAtMs", "region"]);
  assert.equal(ready.ready, true);
  assert.equal(ready.region, true);
  assert.ok(ready.dryBound! > 0 && ready.wetBound! > ready.dryBound!);
  assert.ok(JSON.stringify(ready).length < 200, "sixteen of these must fit the receiver's 4 kB document");

  const knocked = regionAutoCalRow(at(alocasia, "2026-08-29T06:00:00Z"));
  assert.equal(knocked.ready, false);
  assert.equal(knocked.region, false);
  assert.equal(knocked.dryBound, 0);
});
