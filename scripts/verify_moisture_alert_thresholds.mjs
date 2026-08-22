import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");

const firmware = await readFile(join(repoRoot, "arduino", "MSV2_ESP32_Display_Runtime", "MSV2_ESP32_Display_Runtime.ino"), "utf8");
const localApp = await readFile(join(repoRoot, "arduino", "MSV2_ESP32_Display_Runtime", "MobileAppPage.h"), "utf8");
const cloudApp = await readFile(join(repoRoot, "web", "plant-dashboard", "assets", "app.v14.js"), "utf8");
const dashboardHtml = await readFile(join(repoRoot, "web", "plant-dashboard", "index.html"), "utf8");
const serviceWorker = await readFile(join(repoRoot, "web", "plant-dashboard", "sw.js"), "utf8");

assert.match(
  firmware,
  /uint16_t moistureHealthColor[\s\S]*?if \(percent == 0\)[\s\S]*?return dangerColor;[\s\S]*?if \(percent <= 10\)[\s\S]*?return warningColor;/,
  "Firmware moisture colors must be red only at 0% and yellow only through 10%.",
);
assert.match(
  firmware,
  /bool slotNeedsBlink[\s\S]*?return percent == 0;/,
  "Physical-display blinking and its shared buzzer trigger must activate only at 0%.",
);
assert.match(
  firmware,
  /uint16_t batteryHealthColor[\s\S]*?if \(percent < 25\)[\s\S]*?if \(percent < 50\)/,
  "Moisture threshold changes must not silently alter battery colors.",
);
assert.ok(
  localApp.includes('p === 0 ? "danger" : p <= 10 ? "warn"') &&
    cloudApp.includes('if (pct === 0) return "danger";') &&
    cloudApp.includes('if (pct <= 10) return "warn";'),
  "Local and cloud web apps must use the same 0% red / 1-10% yellow thresholds.",
);
assert.ok(
  dashboardHtml.includes('src="assets/app.v14.js"') &&
    serviceWorker.includes('"msv2-cloud-app-v14"') &&
    serviceWorker.includes('"/assets/app.v14.js"'),
  "Dashboard HTML and service-worker cache must reference the v14 cache-busted app.",
);

console.log("CHECK moisture alert thresholds agree across firmware and both web clients");
