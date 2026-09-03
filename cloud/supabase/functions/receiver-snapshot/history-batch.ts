export const MAX_HISTORY_ROWS_PER_UPLOAD = 128;
export const MAX_QUEUED_AGE_MS = 7 * 24 * 60 * 60 * 1000;
export const MAX_ABSOLUTE_HISTORY_AGE_MS = 90 * 24 * 60 * 60 * 1000;
export const MAX_OBSERVED_FUTURE_SKEW_MS = 5 * 60 * 1000;

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

export function mergeLatestPlants<T extends { sensor?: unknown }>(
  existingPlants: T[] | null | undefined,
  latestPlants: T[] | null | undefined,
): T[] {
  const merged = new Map<number, T>();
  for (const plant of Array.isArray(existingPlants) ? existingPlants : []) {
    const sensorId = Number(plant?.sensor);
    if (isValidInt32(sensorId) && sensorId > 0) merged.set(sensorId, plant);
  }
  // Bound the receiver-supplied latest cache independently of historyBatch. The current
  // firmware has at most 16 sensors; 128 leaves rollout room without accepting an
  // unbounded object merge from the network.
  for (const plant of (Array.isArray(latestPlants) ? latestPlants : []).slice(0, 128)) {
    const sensorId = Number(plant?.sensor);
    if (isValidInt32(sensorId) && sensorId > 0) merged.set(sensorId, plant);
  }
  return [...merged.entries()].sort(([left], [right]) => left - right).map(([, plant]) => plant);
}

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

export function observedAtIso(serverNowMs: number, queuedAgeMs: unknown, observedAtMs?: unknown): string {
  // Durable receiver journals can outlive a reboot, while millis()/queuedAgeMs cannot.
  // Prefer the receiver's server-anchored absolute time when it falls in a conservative
  // window. Values outside the window are untrusted input and use the bounded legacy age.
  const absoluteMs = Number(observedAtMs);
  if (
    Number.isSafeInteger(absoluteMs) &&
    absoluteMs >= serverNowMs - MAX_ABSOLUTE_HISTORY_AGE_MS &&
    absoluteMs <= serverNowMs + MAX_OBSERVED_FUTURE_SKEW_MS
  ) {
    return new Date(absoluteMs).toISOString();
  }
  const numericAge = Number(queuedAgeMs);
  const ageMs = Number.isFinite(numericAge)
    ? Math.min(Math.max(Math.trunc(numericAge), 0), MAX_QUEUED_AGE_MS)
    : 0;
  return new Date(serverNowMs - ageMs).toISOString();
}
