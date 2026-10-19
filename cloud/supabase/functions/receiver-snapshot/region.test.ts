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
  // Waterings 4 and 5 run longer here, 50 and 25 h: the probe was knocked on 08-28 and reads lower
  // since. That is no longer called disturbed (2026-10-18), so each later watering settles lower
  // than the scale in force and only half of the fall is taken; the reference takes all of it.
  const second = saidBefore(alocasia, "2026-08-24T19:35:00Z");
  assert.equal(second.said, "water now");
  assert.ok(second.hours >= 15 && second.hours <= 24, `watering 2: ${second.hours} h`);

  const fourth = saidBefore(alocasia, "2026-09-07T20:20:00Z");
  assert.equal(fourth.said, "water now");
  assert.ok(fourth.hours >= 46 && fourth.hours <= 54, `watering 4: ${fourth.hours} h`);

  const fifth = saidBefore(alocasia, "2026-09-15T01:40:00Z");
  assert.equal(fifth.said, "water now");
  assert.ok(fifth.hours >= 21 && fifth.hours <= 29, `watering 5: ${fifth.hours} h`);

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

test("a knock a drying pot could also make keeps its region (the user can see a knocked probe)", () => {
  // 08-28: 1000 -> 880 inside an hour, 28 % of the scale. Disturbed under the 20 % rule until
  // 2026-10-18; the maintainer would rather keep the calibration.
  const knocked = at(alocasia, "2026-08-29T06:00:00Z");
  assert.equal(knocked.ready, true);
  assert.equal(knocked.disturbed, false);

  const settledAgain = at(alocasia, "2026-08-31T12:00:00Z");
  assert.equal(settledAgain.ready, true);
  assert.equal(settledAgain.waterings[2].taughtDrop, null);
  assert.match(settledAgain.waterings[2].verdict, /half of the fall taken/);
});

// Probe 7, 2026-09-22 to 2026-10-18 (live hourly medians, stored anchor as in the cloud). Watered
// 10-11, 10-14 and 10-17. On 10-18 the noon drydown fell 502 in an hour, 22 % of its scale, and
// the old 20 % guard called it disturbed (maintainer: the water spreads through the pot); before
// that, each watering had been held as a shift on an older range and never resolved.
const probe7 = JSON.parse(
  readFileSync(new URL("./fixtures/region-probe7-hourly.json", import.meta.url), "utf8"),
) as RegionRawSensor;

test("probe 7 keeps its calibration through the drydown after a watering", () => {
  // 30 h after the 10-17 watering: settled 44,822, reading 43,675.
  const nextDay = at(probe7, "2026-10-19T03:00:00Z");
  assert.equal(nextDay.disturbed, false);
  assert.equal(nextDay.ready, true);
  assert.match(nextDay.waterings.at(-1)!.verdict, /the new scale/);
  const reading = probe7.hours.at(-1)![1];
  const percent = 100 * (reading - nextDay.top!) / (nextDay.settled! - nextDay.top!);
  assert.ok(percent > 5 && percent < 40, `probe 7 at ${percent.toFixed(0)} %`);

  // Before each of its real waterings it was above the overdue line.
  for (const iso of ["2026-10-11T22:00:00Z", "2026-10-15T06:00:00Z", "2026-10-17T20:00:00Z"]) {
    const hour = Date.parse(iso) / HOUR_MS - 1;
    const before = deriveRegion({ ...probe7, hours: probe7.hours.filter((h) => h[0] <= hour) }, (hour + 1) * HOUR_MS);
    const level = probe7.hours.find((h) => h[0] === hour)![1];
    assert.ok(before.ready && level >= before.bottom!, `overdue before ${iso}`);
  }
});

test("a fertilized watering moves the region with the scale as soon as it settles", () => {
  // Until 2026-10-18 this was held as a shift for a day first and reached the same numbers on 09-25.
  const settled = at(alocasia, "2026-09-23T23:00:00Z");
  assert.match(settled.waterings[5].verdict, /the new scale/);
  assert.ok(Math.abs(settled.top! - 976) <= 4, `rescaled top ${settled.top}`);
  assert.ok(Math.abs(settled.bottom! - 890) <= 4, `rescaled bottom ${settled.bottom}`);
  assert.ok(Math.abs(settled.settled! - 1381) <= 3);
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

  const unsettled = regionAutoCalRow(at(alocasia, "2026-08-12T12:00:00Z"));
  assert.equal(unsettled.ready, false);
  assert.equal(unsettled.region, false);
  assert.equal(unsettled.dryBound, 0);
});
