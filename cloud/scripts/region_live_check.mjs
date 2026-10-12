#!/usr/bin/env node
// What receiver-snapshot?autocal=1&region=1 would answer for a receiver right now: the live
// msv2_read_sensor_hourly_moisture() result run through the edge function's own region.ts.
// The receiver's token is not needed; this goes through the linked Supabase CLI. Calling the
// RPC refreshes the trailing hours of the hourly table, exactly as a receiver's pull does.
//
//   node cloud/scripts/region_live_check.mjs MSV2088A3104A7AC
import { execSync } from "node:child_process";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { deriveRegion, regionAutoCalRow } from "../supabase/functions/receiver-snapshot/region.ts";

const receiverId = process.argv[2] ?? "";
if (!/^[A-Za-z0-9_-]+$/.test(receiverId)) {
  console.error("usage: node cloud/scripts/region_live_check.mjs <receiverId>");
  process.exit(2);
}
const workdir = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const started = Date.now();
// One command string: on Windows the CLI is a .cmd, which Node only runs through a shell, and a
// shell does not quote an argument list. receiverId is checked above; nothing else varies.
const stdout = execSync(
  `supabase db query --linked --workdir "${workdir}" "select public.msv2_read_sensor_hourly_moisture('${receiverId}') as sensors;"`,
  { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 },
);
const rows = JSON.parse(stdout.slice(stdout.indexOf("{"))).rows ?? [];
let sensors = rows[0]?.sensors ?? [];
if (typeof sensors === "string") sensors = JSON.parse(sensors);
console.log(`${sensors.length} sensors, ${Date.now() - started} ms for the query and the CLI`);

const nowMs = Date.now();
const local = (ms) => ms === null ? "-" : new Date(ms).toLocaleString("sv-SE", { timeZone: "America/Los_Angeles" }).slice(5, 16);
for (const raw of sensors) {
  const region = deriveRegion(raw, nowMs);
  const row = regionAutoCalRow(region);
  const last = raw.hours.at(-1);
  const percent = region.ready ? Math.min(100, Math.round(100 * (last[1] - region.top) / (region.settled - region.top))) : null;
  console.log(
    `sensor ${String(raw.sensorId).padStart(2)}  scale ${raw.scale}  ${String(raw.hours.length).padStart(4)} h  mode ${region.mode.padEnd(5)}` +
    `${region.base === null ? "" : ` base ${region.base}`}  reading ${last?.[1] ?? "-"}  ` +
    (region.ready
      ? `READY  100 % at ${region.settled}, 0 % (water now) at ${region.top}, overdue below ${region.bottom}  -> ${percent} %`
      : `not ready: ${region.reason}`) +
    `  | taught ${region.taught}${region.anchorStored ? " (stored)" : ""}, last watered ${local(region.lastWateredAtMs)}, ${region.waterings.length} waterings`,
  );
  for (const watering of region.waterings.slice(-4)) {
    console.log(`      ${local(watering.atMs)}  before ${Math.round(watering.levelBefore)}  settled ${watering.settled === null ? "-" : Math.round(watering.settled)}  ${watering.verdict}`);
  }
  if (process.argv.includes("--rows")) console.log("      row:", JSON.stringify(row));
}
