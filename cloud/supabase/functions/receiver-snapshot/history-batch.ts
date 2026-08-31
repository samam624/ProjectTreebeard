export const MAX_HISTORY_ROWS_PER_UPLOAD = 128;
export const MAX_QUEUED_AGE_MS = 7 * 24 * 60 * 60 * 1000;

// msv2_store_sensor_reading_batch (migration 035) casts every one of these fields with
// `::integer` in a loop with no per-row exception handler - a non-integer-formatted value
// (e.g. a stray "3.5") or one outside Postgres's int4 range throws and rolls back the WHOLE
// batch transaction, not just the offending row, and the caller (index.ts) treats that as a
// transient error and has the receiver retry the same poisoned batch forever. Number.isFinite
// alone (the previous check) accepts both those cases. Use this for every field whose column
// is `integer`.
const INT4_MIN = -2147483648;
const INT4_MAX = 2147483647;
export function isValidInt32(value: number): boolean {
  return Number.isInteger(value) && value >= INT4_MIN && value <= INT4_MAX;
}

// rawFdcCounts maps to a `bigint` column, but is physically the FDC2214's 28-bit raw count
// register (see MobileAppPage.h's own maxFdcCount = 0x0FFFFFFF for the same bound) - far
// below where Number's float precision would lose integer accuracy, and never negative.
const RAW_FDC_COUNT_MAX = 0x0fffffff;
export function isValidRawFdcCount(value: number): boolean {
  return Number.isSafeInteger(value) && value >= 0 && value <= RAW_FDC_COUNT_MAX;
}

export type HistoryBatchEnvelope<T> = {
  plants?: T[];
  historyBatch?: T[];
};

export function selectHistoryRows<T>(snapshot: HistoryBatchEnvelope<T>): {
  usesBatch: boolean;
  rows: T[];
} {
  const usesBatch = Array.isArray(snapshot.historyBatch);
  const source = usesBatch ? snapshot.historyBatch! : Array.isArray(snapshot.plants) ? snapshot.plants : [];
  return {
    usesBatch,
    rows: source.slice(0, MAX_HISTORY_ROWS_PER_UPLOAD),
  };
}

export function normalizeObservationId(value: unknown): string | null {
  const normalized = String(value ?? "").trim();
  if (!normalized || normalized.length > 96) return null;
  return normalized;
}

// A batch row that fails this check is unrecoverably malformed - retrying it will never
// succeed, so the caller should drop it (not the whole batch) and still count it toward
// the accepted-row total returned to the receiver, which only knows how to acknowledge a
// contiguous prefix of what it sent.
export function validateHistoryRowIdentity(
  plant: { sensor?: unknown; observationId?: unknown } | null | undefined,
): { sensorId: number; observationId: string } | null {
  const sensorId = Number(plant?.sensor);
  if (!isValidInt32(sensorId) || sensorId <= 0) return null;

  const observationId = normalizeObservationId(plant?.observationId);
  if (!observationId) return null;

  return { sensorId, observationId };
}

export function observedAtIso(serverNowMs: number, queuedAgeMs: unknown): string {
  const numericAge = Number(queuedAgeMs);
  const ageMs = Number.isFinite(numericAge)
    ? Math.min(Math.max(Math.trunc(numericAge), 0), MAX_QUEUED_AGE_MS)
    : 0;
  return new Date(serverNowMs - ageMs).toISOString();
}
