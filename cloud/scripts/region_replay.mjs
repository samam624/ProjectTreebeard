#!/usr/bin/env node
// Run the edge function's deriveRegion() (supabase/functions/receiver-snapshot/region.ts) forward
// through a stored record, hour by hour, each hour seeing only the hours before it.
//
//   python cloud/scripts/fetch_moisture_buckets.py --receiver MSV2088A3104A7AC --sensors 3 10
//   node cloud/scripts/region_replay.mjs --sensor 3 --base 730 --from 2026-08-09T08:15Z --to 2026-10-01T01:00Z --clip 700,1600
//   node cloud/scripts/region_replay.mjs --sensor 10 --scale 4
//
// --fixture <file> writes the hourly medians it used, for region.test.ts.
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { deriveRegion } from "../supabase/functions/receiver-snapshot/region.ts";

const here = dirname(fileURLToPath(import.meta.url));
const args = Object.fromEntries(process.argv.slice(2).reduce((pairs, word, index, all) => {
  if (word.startsWith("--")) pairs.push([word.slice(2), all[index + 1] && !all[index + 1].startsWith("--") ? all[index + 1] : "1"]);
  return pairs;
}, []));
const sensorId = Number(args.sensor ?? 3);
const csvPath = args.csv ?? join(resolve(here, "..", ".."), "data", "sensor_logs", `moisture_5min_s${sensorId}.csv`);
const from = args.from ? Date.parse(args.from) : -Infinity;
const to = args.to ? Date.parse(args.to) : Infinity;
const [clipLow, clipHigh] = (args.clip ?? "-Infinity,Infinity").split(",").map(Number);

export function hourlyMedians(csvText, { from = -Infinity, to = Infinity, clipLow = -Infinity, clipHigh = Infinity } = {}) {
  const byHour = new Map();
  for (const line of csvText.trim().split(/\r?\n/).slice(1)) {
    const [stamp, value] = line.split(",");
    const at = Date.parse(stamp.replace(" ", "T").replace(/\+00$/, "Z"));
    const moisture = Number(value);
    if (!(at >= from && at <= to) || !(moisture >= clipLow && moisture <= clipHigh)) continue;
    const hour = Math.floor(at / 3600_000);
    if (!byHour.has(hour)) byHour.set(hour, []);
    byHour.get(hour).push(moisture);
  }
  return [...byHour.entries()].sort((a, b) => a[0] - b[0]).map(([hour, values]) => {
    values.sort((a, b) => a - b);
    const middle = Math.floor(values.length / 2);
    return [hour, Math.round(values.length % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2)];
  });
}

const local = (ms) => new Date(ms).toLocaleString("sv-SE", { timeZone: "America/Los_Angeles" }).slice(5, 16);

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const hours = hourlyMedians(readFileSync(csvPath, "utf8"), { from, to, clipLow, clipHigh });
  const sensor = {
    sensorId,
    scale: args.scale ? Number(args.scale) : null,
    regionBase: args.base ? Number(args.base) : null,
    regionDrop: args.drop ? Number(args.drop) : null,
    regionMode: args.mode ?? null,
  };
  if (args.fixture) writeFileSync(args.fixture, JSON.stringify({ ...sensor, hours }) + "\n");

  // What the region said during each hour, judged with what was known when that hour closed.
  const said = [];
  let last = null;
  for (let j = 0; j < hours.length; j++) {
    last = deriveRegion({ ...sensor, hours: hours.slice(0, j + 1) }, (hours[j][0] + 1) * 3600_000);
    const reading = hours[j][1];
    said.push(!last.ready ? "-" : reading < last.bottom ? "overdue" : reading <= last.top ? "water now" : "not yet");
  }
  console.log(`sensor ${sensorId}: ${hours.length} hours, mode ${last.mode}${last.base !== null ? `, base ${last.base}` : ""}`);
  console.log(" #  watered      before  settled  taught   first day  on for  said     verdict");
  last.waterings.forEach((watering, n) => {
    const index = hours.findIndex((row) => row[0] * 3600_000 >= watering.atMs);
    let onFor = 0;
    for (let k = index - 1; k >= 0 && (said[k] === "water now" || said[k] === "overdue"); k--) onFor++;
    const before = index > 0 ? said[index - 1] : "-";
    console.log(
      `${String(n + 1).padStart(2)}  ${local(watering.atMs)}  ${String(Math.round(watering.levelBefore)).padStart(6)}  ` +
      `${String(watering.settled === null ? "-" : Math.round(watering.settled)).padStart(7)}  ` +
      `${(watering.taughtDrop === null ? "-" : watering.taughtDrop.toFixed(last.mode === "share" ? 3 : 0)).padStart(6)}  ` +
      `${(watering.firstDay === null ? "-" : watering.firstDay.toFixed(3)).padStart(9)}  ${String(onFor).padStart(4)} h  ${before.padEnd(9)} ${watering.verdict}`,
    );
  });
  const now = hours[hours.length - 1];
  const percent = last.ready ? Math.min(100, Math.round(100 * (now[1] - last.top) / (last.settled - last.top))) : null;
  console.log(
    `as of ${local((now[0] + 1) * 3600_000)}: ready ${last.ready} (${last.reason}); reading ${now[1]}; settled ${last.settled}; ` +
    `top ${last.top}; bottom ${last.bottom}; drop ${last.drop === null ? "-" : last.drop.toFixed(last.mode === "share" ? 3 : 0)}; ` +
    `taught ${last.taught}; percent ${percent}; anchor to store ${last.anchorToStore === null ? "-" : last.anchorToStore.toFixed(3)}`,
  );
  const counts = said.reduce((all, state) => ({ ...all, [state]: (all[state] ?? 0) + 1 }), {});
  console.log("hours said:", JSON.stringify(counts));
}
