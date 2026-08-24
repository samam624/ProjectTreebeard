import assert from "node:assert/strict";
import test from "node:test";

import {
  MAX_HISTORY_ROWS_PER_UPLOAD,
  MAX_QUEUED_AGE_MS,
  normalizeObservationId,
  observedAtIso,
  selectHistoryRows,
  validateHistoryRowIdentity,
} from "./history-batch.ts";

test("an explicit empty batch suppresses lossy latest-plant history fallback", () => {
  const selection = selectHistoryRows({ plants: [{ sensor: 1 }], historyBatch: [] });
  assert.equal(selection.usesBatch, true);
  assert.deepEqual(selection.rows, []);
});

test("legacy receivers still store latest plants when no batch field exists", () => {
  const plants = [{ sensor: 1 }, { sensor: 2 }];
  const selection = selectHistoryRows({ plants });
  assert.equal(selection.usesBatch, false);
  assert.deepEqual(selection.rows, plants);
});

test("history uploads are bounded server-side", () => {
  const historyBatch = Array.from({ length: MAX_HISTORY_ROWS_PER_UPLOAD + 5 }, (_, index) => index);
  assert.equal(selectHistoryRows({ historyBatch }).rows.length, MAX_HISTORY_ROWS_PER_UPLOAD);
});

test("queued age reconstructs observation time and clamps untrusted values", () => {
  const now = Date.parse("2026-08-23T05:00:00.000Z");
  assert.equal(observedAtIso(now, 60_000), "2026-08-23T04:59:00.000Z");
  assert.equal(observedAtIso(now, -50), "2026-08-23T05:00:00.000Z");
  assert.equal(observedAtIso(now, MAX_QUEUED_AGE_MS + 1), new Date(now - MAX_QUEUED_AGE_MS).toISOString());
});

test("observation ids are required and bounded for idempotent batches", () => {
  assert.equal(normalizeObservationId("  ABC-123  "), "ABC-123");
  assert.equal(normalizeObservationId(""), null);
  assert.equal(normalizeObservationId("x".repeat(97)), null);
});

test("a malformed row's identity is rejected without touching a well-formed sibling", () => {
  assert.deepEqual(validateHistoryRowIdentity({ sensor: 3, observationId: "boot1-42" }), {
    sensorId: 3,
    observationId: "boot1-42",
  });
  assert.equal(validateHistoryRowIdentity({ sensor: 0, observationId: "boot1-42" }), null);
  assert.equal(validateHistoryRowIdentity({ sensor: -1, observationId: "boot1-42" }), null);
  assert.equal(validateHistoryRowIdentity({ sensor: Number.NaN, observationId: "boot1-42" }), null);
  assert.equal(validateHistoryRowIdentity({ sensor: 3, observationId: "" }), null);
  assert.equal(validateHistoryRowIdentity({ sensor: 3 }), null);
  assert.equal(validateHistoryRowIdentity(null), null);
});

test("a batch with one malformed row still processes every other row (the receiver-snapshot fix this backs)", () => {
  // Mirrors the loop in index.ts: rows failing identity validation are dropped and
  // counted separately, everything else is queued for the RPC - one bad row must never
  // discard its well-formed siblings.
  const rows = [
    { sensor: 1, observationId: "boot1-1" },
    { sensor: 0, observationId: "boot1-2" }, // malformed: sensor <= 0
    { sensor: 2, observationId: "" }, // malformed: no observationId
    { sensor: 3, observationId: "boot1-4" },
  ];
  const valid = [];
  let malformedDropped = 0;
  for (const row of rows) {
    const identity = validateHistoryRowIdentity(row);
    if (!identity) {
      malformedDropped++;
      continue;
    }
    valid.push(identity);
  }
  assert.equal(malformedDropped, 2);
  assert.deepEqual(
    valid.map((v) => v.sensorId),
    [1, 3],
  );
  // historyAccepted (stored + malformedDropped) must equal the original row count so the
  // receiver's contiguous-prefix ack can safely advance past the whole batch.
  assert.equal(valid.length + malformedDropped, rows.length);
});
