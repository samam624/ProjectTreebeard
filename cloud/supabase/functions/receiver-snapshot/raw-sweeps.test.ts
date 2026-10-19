import assert from "node:assert/strict";
import test from "node:test";

import { MAX_RAW_SWEEP_AGE_MS, selectRawSweepRows } from "./raw-sweeps.ts";

const HEX = "0123456789abcdef0123456789abcdef0123456789abcdef";
const plant = (sensor: unknown, sweep: unknown) => ({ sensor, raw: { msv4: { sweep } } });

test("a full snapshot's sweep becomes one row stamped at server time minus its age", () => {
  const rows = selectRawSweepRows({ plants: [plant(11, { wake: 1234, ageMs: 2500, hex: HEX })] }, 1_000_000);
  assert.deepEqual(rows, [{ sensor: 11, wake: 1234, hex: HEX, observedAtMs: 997_500 }]);
});

test("history-only latestPlants are read too, and hex is lower-cased", () => {
  const rows = selectRawSweepRows({ latestPlants: [plant(11, { wake: 7, ageMs: 0, hex: HEX.toUpperCase() })] }, 50);
  assert.deepEqual(rows, [{ sensor: 11, wake: 7, hex: HEX, observedAtMs: 50 }]);
});

test("the same sweep in plants and latestPlants is stored once", () => {
  const sweep = { wake: 9, ageMs: 10, hex: HEX };
  const rows = selectRawSweepRows({ plants: [plant(11, sweep)], latestPlants: [plant(11, sweep)] }, 100);
  assert.equal(rows.length, 1);
});

test("plants without a sweep, and every malformed field, are dropped alone", () => {
  const good = plant(11, { wake: 1, ageMs: 0, hex: HEX });
  const bad = [
    { sensor: 10, raw: { msv4: { cmDeciPf: 300 } } },
    { sensor: 3, raw: { fdcCounts: 5 } },
    plant(0, { wake: 1, ageMs: 0, hex: HEX }),
    plant(17, { wake: 1, ageMs: 0, hex: HEX }),
    plant("11", { wake: -1, ageMs: 0, hex: HEX }),
    plant(11, { wake: 1.5, ageMs: 0, hex: HEX }),
    plant(11, { wake: 2, ageMs: -1, hex: HEX }),
    plant(11, { wake: 3, ageMs: MAX_RAW_SWEEP_AGE_MS + 1, hex: HEX }),
    plant(11, { wake: 4, ageMs: 0, hex: HEX.slice(1) }),
    plant(11, { wake: 5, ageMs: 0, hex: HEX.slice(0, 47) + "g" }),
    plant(11, { wake: 6, ageMs: 0, hex: 12345 }),
    plant(11, null),
  ];
  const rows = selectRawSweepRows({ plants: [...bad, good] }, 0);
  assert.deepEqual(rows.map((r) => r.wake), [1]);
});

test("a snapshot with no plant arrays yields nothing", () => {
  assert.deepEqual(selectRawSweepRows({}, 0), []);
  assert.deepEqual(selectRawSweepRows({ plants: "nope" as unknown }, 0), []);
});
