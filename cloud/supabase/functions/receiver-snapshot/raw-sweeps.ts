// MSV4 raw sweeps (migration 042). The receiver forwards the latest scan-response sweep of each
// MSV4 probe as plants[].raw.msv4.sweep = { wake, ageMs, hex } in both the full snapshot (plants)
// and the compact history-only upload (latestPlants). Every field is checked here, so a malformed
// sweep is dropped alone and msv4_store_raw_sweeps() never sees a value its casts or the table's
// checks would reject.

import { isValidInt32 } from "./history-batch.ts";

export const RAW_SWEEP_HEX_LENGTH = 48; // 8 points x 3 bytes
const RAW_SWEEP_HEX = /^[0-9a-fA-F]{48}$/;
// The receiver drops a sweep it has held longer than this; anything older is a stale echo.
export const MAX_RAW_SWEEP_AGE_MS = 60 * 60 * 1000;

export type RawSweepRow = {
  sensor: number;
  wake: number;
  hex: string;
  observedAtMs: number;
};

type SweepCarrier = {
  sensor?: unknown;
  raw?: { msv4?: { sweep?: { wake?: unknown; ageMs?: unknown; hex?: unknown } } };
};

export function selectRawSweepRows(
  snapshot: { plants?: unknown; latestPlants?: unknown },
  serverNowMs: number,
): RawSweepRow[] {
  const rows = new Map<string, RawSweepRow>();
  for (const list of [snapshot.plants, snapshot.latestPlants]) {
    if (!Array.isArray(list)) continue;
    for (const plant of list as SweepCarrier[]) {
      const sweep = plant?.raw?.msv4?.sweep;
      if (!sweep || typeof sweep !== "object") continue;
      const sensor = Number(plant.sensor);
      const wake = Number(sweep.wake);
      const ageMs = Number(sweep.ageMs);
      const hex = typeof sweep.hex === "string" ? sweep.hex.toLowerCase() : "";
      if (!isValidInt32(sensor) || sensor < 1 || sensor > 16) continue;
      if (!isValidInt32(wake) || wake < 0) continue;
      if (!isValidInt32(ageMs) || ageMs < 0 || ageMs > MAX_RAW_SWEEP_AGE_MS) continue;
      if (!RAW_SWEEP_HEX.test(hex)) continue;
      rows.set(`${sensor}:${wake}:${hex}`, { sensor, wake, hex, observedAtMs: serverNowMs - ageMs });
    }
  }
  return [...rows.values()];
}
