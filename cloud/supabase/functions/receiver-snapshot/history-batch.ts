export const MAX_HISTORY_ROWS_PER_UPLOAD = 128;
export const MAX_QUEUED_AGE_MS = 7 * 24 * 60 * 60 * 1000;

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
  if (!Number.isFinite(sensorId) || sensorId <= 0) return null;

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
